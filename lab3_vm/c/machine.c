#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NREG (32)
#define PAGESIZE_WIDTH (2)
#define PAGESIZE (1 << PAGESIZE_WIDTH)
#define NPAGES (2048)
#define RAM_PAGES (8)
#define RAM_SIZE (RAM_PAGES * PAGESIZE)
#define SWAP_PAGES (128)
#define SWAP_SIZE (SWAP_PAGES * PAGESIZE)
#undef DEBUG
#define MAX_TRACE 1000

#define ADD (0)
#define ADDI (1)
#define SUB (2)
#define SUBI (3)
#define SGE (4)
#define SGT (5)
#define SEQ (6)
#define BT (7)
#define BF (8)
#define BA (9)
#define ST (10)
#define LD (11)
#define CALL (12)
#define JMP (13)
#define MUL (14)
#define SEQI (15)
#define HALT (16)

char *mnemonics[] = {
    [ADD] = "add",
    [ADDI] = "addi",
    [SUB] = "sub",
    [SUBI] = "subi",
    [SGE] = "sge",
    [SGT] = "sgt",
    [SEQ] = "seq",
    [SEQI] = "seqi",
    [BT] = "bt",
    [BF] = "bf",
    [BA] = "ba",
    [ST] = "st",
    [LD] = "ld",
    [CALL] = "call",
    [JMP] = "jmp",
    [MUL] = "mul",
    [HALT] = "halt",
};

typedef struct
{
    unsigned pc;        /* Program counter. */
    unsigned reg[NREG]; /* Registers. */
} cpu_t;

typedef struct
{
    unsigned int page : 27;      /* Swap or RAM page. */
    unsigned int inmemory : 1;   /* Page is in memory. */
    unsigned int ondisk : 1;     /* Page is on disk. */
    unsigned int modified : 1;   /* Page was modified while in memory. */
    unsigned int referenced : 1; /* Page was referenced recently. */
    unsigned int readonly : 1;   /* Error if written to (not checked). */
} page_table_entry_t;

typedef struct
{
    page_table_entry_t *owner; /* Owner of this phys page. */
    unsigned page;             /* Swap page of page if assigned. */
} coremap_entry_t;

static unsigned long long num_pagefault;      /* Statistics. */
static page_table_entry_t page_table[NPAGES]; /* OS data structure. */
static coremap_entry_t coremap[RAM_PAGES];    /* OS data structure. */
static unsigned memory[RAM_SIZE];             /* Hardware: RAM. */
static unsigned swap[SWAP_SIZE];              /* Hardware: disk. */
static unsigned (*replace)(void);             /* Page repl. alg. */

int x;
static int diskwrites = 0;
static unsigned trace[MAX_TRACE];
static size_t trace_index = 0;

unsigned make_instr(unsigned opcode, unsigned dest, unsigned s1, unsigned s2)
{
    return (opcode << 26) | (dest << 21) | (s1 << 16) | (s2 & 0xffff);
}

unsigned extract_opcode(unsigned instr) { return instr >> 26; }

unsigned extract_dest(unsigned instr) { return (instr >> 21) & 0x1f; }

unsigned extract_source1(unsigned instr) { return (instr >> 16) & 0x1f; }

signed extract_constant(unsigned instr) { return (short)(instr & 0xffff); }

void error(char *fmt, ...)
{
    va_list ap;
    char buf[BUFSIZ];

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    fprintf(stderr, "error: %s\n", buf);
    exit(1);
}

static void read_page(unsigned phys_page, unsigned swap_page)
{
    memcpy(&memory[phys_page * PAGESIZE], &swap[swap_page * PAGESIZE],
           PAGESIZE * sizeof(unsigned));
}

static void write_page(unsigned phys_page, unsigned swap_page)
{
    memcpy(&swap[swap_page * PAGESIZE], &memory[phys_page * PAGESIZE],
           PAGESIZE * sizeof(unsigned));
    ++diskwrites;
}

static unsigned new_swap_page()
{
    static int count;

    assert(count < SWAP_PAGES);

    return count++;
}

static unsigned fifo_page_replace()
{
    static unsigned next_page = 0;
    unsigned page = next_page;
    next_page = (next_page + 1) % RAM_PAGES;

    return page;
}

static unsigned second_chance_replace()
{
    coremap_entry_t *entry;
    static int next_page = -1; 

    for(;;)
    {
        next_page = (next_page + 1) % RAM_PAGES;
        entry = &coremap[next_page];
        if (entry->owner == NULL || entry->owner->referenced)
        {
            break;
        }
        entry->owner->referenced = 0;
    }
    return next_page;
}


static unsigned take_phys_page()
{
    unsigned page = (*replace)();
    coremap_entry_t *entry = &coremap[page];

    if (entry->owner == NULL)
    {
        return page;
    }
    
    if (entry->owner->ondisk)
    {
        if (entry->owner->modified)
        {
            write_page(page, entry->owner->page);
        }
        entry->owner->page = entry->page;
    }
    else
    {
        unsigned swap = new_swap_page();
        entry->owner->page = swap;
        write_page(page, swap);
    }
    
    entry->owner->inmemory = 0;
    entry->owner->modified = 0;
    entry->owner->referenced = 0;
    entry->owner->ondisk = 1;

    return page;
}

static void pagefault(unsigned virt_page)
{
    num_pagefault++;

    unsigned page = take_phys_page();
    page_table_entry_t *virt_page_entry = &page_table[virt_page];
    coremap_entry_t *entry = &coremap[page];

    if (virt_page_entry->ondisk)
    {
        entry->page = virt_page_entry->page;
        read_page(page, virt_page_entry->page);
    }

    virt_page_entry->inmemory = 1;
    virt_page_entry->page = page;
    entry->owner = virt_page_entry;

}

static void translate(unsigned virt_addr, unsigned *phys_addr, bool write)
{
    unsigned virt_page;
    unsigned offset;

    virt_page = virt_addr / PAGESIZE;
    offset = virt_addr & (PAGESIZE - 1);

    // printf("page number: %d\n", virt_page);
    if (trace_index < MAX_TRACE)
    {
        trace[trace_index++] = virt_page;
    }

    if (!page_table[virt_page].inmemory)
        pagefault(virt_page);

    page_table[virt_page].referenced = 1;

    if (write)
        page_table[virt_page].modified = 1;

    *phys_addr = page_table[virt_page].page * PAGESIZE + offset;
}

static unsigned read_memory(unsigned *memory, unsigned addr)
{
    unsigned phys_addr;

    translate(addr, &phys_addr, false);

    return memory[phys_addr];
}

static void write_memory(unsigned *memory, unsigned addr, unsigned data)
{
    unsigned phys_addr;

    translate(addr, &phys_addr, true);

    memory[phys_addr] = data;
}

void read_program(char *file, unsigned memory[], int *ninstr)
{
    FILE *in;
    int opcode;
    int a, b, c;
    int i;
    char buf[BUFSIZ];
    char text[BUFSIZ];
    int n;
    int line;

    /* Find out the number of mnemonics. */
    n = sizeof mnemonics / sizeof mnemonics[0];

    in = fopen(file, "r");

    if (in == NULL)
        error("cannot open file");

    line = 0;

    while (fgets(buf, sizeof buf, in) != NULL)
    {
        if (buf[0] == ';')
            continue;

        if (sscanf(buf, "%s %d,%d,%d", text, &a, &b, &c) != 4)
            error("syntax error near: \"%s\"", buf);

        opcode = -1;

        for (i = 0; i < n; ++i)
        {
            if (strcmp(text, mnemonics[i]) == 0)
            {
                opcode = i;
                break;
            }
        }

        if (opcode < 0)
            error("syntax error near: \"%s\"", text);

        write_memory(memory, line, make_instr(opcode, a, b, c));

        line += 1;
    }

    *ninstr = line;
}

int run(int argc, char **argv)
{
    char *file;
    cpu_t cpu;
    int i;
    int j;
    int ninstr;
    unsigned instr;
    unsigned opcode;
    unsigned source_reg1;
    int constant;
    unsigned dest_reg;
    int source1;
    int source2;
    int dest;
    unsigned data;
    bool proceed;
    bool increment_pc;
    bool writeback;

    if (argc > 2)
        file = argv[2];
    else
        file = "a.s";

    read_program(file, memory, &ninstr);

    /* First instruction to execute is at address 0. */
    cpu.pc = 0;
    cpu.reg[0] = 0;

    proceed = true;

    while (proceed)
    {

        /* Fetch next instruction to execute. */
        instr = read_memory(memory, cpu.pc);

        /* Decode the instruction. */
        opcode = extract_opcode(instr);
        source_reg1 = extract_source1(instr);
        constant = extract_constant(instr);
        dest_reg = extract_dest(instr);

        /* Fetch operands. */
        source1 = cpu.reg[source_reg1];
        source2 = cpu.reg[constant & (NREG - 1)];

        increment_pc = true;
        writeback = true;

        printf("pc = %3d: ", cpu.pc);

        switch (opcode)
        {
        case ADD:
            puts("ADD");
            dest = source1 + source2;
            break;

        case ADDI:
            puts("ADDI");
            dest = source1 + constant;
            break;

        case SUB:
            puts("SUB");
            dest = source1 - source2;
            break;

        case SUBI:
            puts("SUBI");
            dest = source1 - constant;
            break;

        case MUL:
            puts("MUL");
            dest = source1 * source2;
            break;

        case SGE:
            puts("SGE");
            dest = source1 >= source2;
            break;

        case SGT:
            puts("SGT");
            dest = source1 > source2;
            break;

        case SEQ:
            puts("SEQ");
            dest = source1 == source2;
            break;

        case SEQI:
            puts("SEQI");
            dest = source1 == constant;
            break;

        case BT:
            puts("BT");
            writeback = false;
            if (source1 != 0)
            {
                cpu.pc = constant;
                increment_pc = false;
            }
            break;

        case BF:
            puts("BF");
            writeback = false;
            if (source1 == 0)
            {
                cpu.pc = constant;
                increment_pc = false;
            }
            break;

        case BA:
            puts("BA");
            writeback = false;
            increment_pc = false;
            cpu.pc = constant;
            break;

        case LD:
            puts("LD");
            data = read_memory(memory, source1 + constant);
            dest = data;
            break;

        case ST:
            puts("ST");
            data = cpu.reg[dest_reg];
            write_memory(memory, source1 + constant, data);
            writeback = false;
            break;

        case CALL:
            puts("CALL");
            increment_pc = false;
            dest = cpu.pc + 1;
            dest_reg = 31;
            cpu.pc = constant;
            break;

        case JMP:
            puts("JMP");
            increment_pc = false;
            writeback = false;
            cpu.pc = source1;
            break;

        case HALT:
            puts("HALT");
            increment_pc = false;
            writeback = false;
            proceed = false;
            break;

        default:
            error("illegal instruction at pc = %d: opcode = %d\n", cpu.pc, opcode);
        }

        if (writeback && dest_reg != 0)
            cpu.reg[dest_reg] = dest;

        if (increment_pc)
            cpu.pc += 1;

#ifdef DEBUG
        i = 0;
        while (i < NREG)
        {
            for (j = 0; j < 4; ++j, ++i)
            {
                if (j > 0)
                    printf("| ");
                printf("R%02d = %-12d", i, cpu.reg[i]);
            }
            printf("\n");
        }
#endif
    }

    i = 0;
    while (i < NREG)
    {
        for (j = 0; j < 4; ++j, ++i)
        {
            if (j > 0)
                printf("| ");
            printf("R%02d = %-12d", i, cpu.reg[i]);
        }
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    replace = fifo_page_replace;
    if (argc >= 2)
    {
        if (!strcmp(argv[1], "--second-chance"))
        {
            replace = second_chance_replace;
            printf("Second change page replacement algorithm.\n");
        }
        else if (!strcmp(argv[1], "--fifo"))
        {
            replace = fifo_page_replace;
            printf("FIFO page replacement algorithm.\n");
        }
        else
        {
            printf("Unknown page replacement algorithm.\n");
            return -1;
        }
    }
    else
    {
        printf("Not enough arguments.\n");
        return -1;
    }

    run(argc, argv);

    printf("%d disk writes\n", diskwrites);
    printf("%llu page faults\n", num_pagefault);
    for (size_t i = 0; i < trace_index; ++i)
    {
        printf("Trace[%zu] = %u\n", i, trace[i]);
    }
}
