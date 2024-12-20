/**
 * Extended from "Simple & Stupid Filesystem": Mohammed Q. Hussain -
 * http://www.maastaar.net 2018-March-05 Flavius Gruian License: GNU GPL
 */

/*
 Overview of tasks:
       [DIR_ENTRY] : add modification time to the directory entry and handle it
 right [LARGE_DIR] : allow the directory to grow over the one block limit (still
 flat) [READ_OFFSET] : correctly implement reading with offset in do_read
       [LARGE_WRITE] : modify do_write to allow writing more than one block
       [TRUNC_FREE] : correctly free the blocks of a file being truncated
       [RENAME] : implement file renaming
       [REMOVE] : implement file deletion
*/
#define FUSE_USE_VERSION 30

#include "fs_support.h"
#include "rawdisk.h"
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <inttypes.h>
#define min(a, b) ((a) < (b) ? (a) : (b))

// The attributes should come from the directory entry.
// TODO: [DIR_ENTRY] add last "m"odification time to the entry and handle it
// properly
static int do_getattr(const char *path, struct stat *st)
{
    //	printf( "[getattr] Called\n" );
    //	printf( "\tAttributes of %s requested\n", path );

    // GNU's definitions of the attributes
    // (http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html):
    // 		st_uid: 	The user ID of the file’s owner.
    //		st_gid: 	The group ID of the file.
    //		st_atime: 	This is the last access time for the file.
    //		st_mtime: 	This is the time of the last modification to the
    // contents of the file. 		st_mode: 	Specifies the mode of the file. This
    // includes file type information (see Testing File Type) and the file
    // permission bits (see Permission Bits). 		st_nlink: 	The number of hard links
    // to the file. This count keeps track of how many directories have entries for
    // this file. If the count is ever decremented to zero, then the file itself is
    // discarded as soon 						as no process still holds it open. Symbolic links are not
    // counted in the total. 		st_size:	This specifies the size of a regular
    // file in bytes. For files that are really devices this field isn’t usually
    // meaningful. For symbolic links this specifies the length of the file name
    // the link refers to.

    st->st_uid = getuid(); // The owner of the file/directory is the user who
                           // mounted the filesystem
    st->st_gid = getgid(); // The group of the file/directory is the same as the
                           // group of the user who mounted the filesystem
    st->st_atime =
        time(NULL); // The last "a"ccess of the file/directory is right now
    st->st_mtime =
        time(NULL); // The last "m"odification of the file/directory is right now

    if (strcmp(path, "/") == 0)
    {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2; // Why "two" hardlinks instead of "one"? The answer is
                          // here: http://unix.stackexchange.com/a/101536
    }
    else
    {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 1024;

        // skip the "/" in the begining
        const char *fn = &path[1];
        // load directory info
        load_directory();
        int di = find_dir_entry(fn);
        if (di >= 0)
        {
            dir_entry *de = index2dir_entry(di);
            printf("  -- %d > %.*s\n", di, FS_NAME_LEN, de->name);
            st->st_size = de->size_bytes;
            st->st_mode = de->mode;
            st->st_mtime = de->modtime;
            st->st_ctime = de->creation_time;
        }
        else
        {
            printf("  -- find_dir_entry cannot find %s\n", fn);
            // this could be a new file. let it through?
            return -ENOENT; // no such file or dir
        }
    }

    return 0;
}

// Loads the unique flat directory (one block) and fills the buffer with
// the right names.
static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi)
{
    printf("--> Getting The List of Files of %s\n", path);

    filler(buffer, ".", NULL, 0);  // Current Directory
    filler(buffer, "..", NULL, 0); // Parent Directory

    if (strcmp(path, "/") ==
        0) // If the user is trying to show the files/directories of the root
           // directory show the following
    {
        // load root directory (block 0 is the block list, block 1 root dir)
        fs_block rootb;
        readBlock(ROOTDIR_BID, rootb.bytes);

        // go through all entries and add them to the list with "filler"
        // note that the number of entries is limited to one block!
        // TODO: [LARGE_DIR] Extend the FS to allow directories larger than one
        // block Hint: linked block lists again
        for (int i = 0;
             i < DIR_ENTRIES_PER_BLOCK && !(dir_entry_is_empty(rootb.directory[i]));
             i++)
        {
            char bnr[FS_NAME_LEN];
            snprintf(bnr, FS_NAME_LEN, "%s", rootb.directory[i].name);
            // printf("   > %d-%s\n",i,bnr);
            filler(buffer, bnr, NULL, 0);
        }
    }
    else
    {
        // it's a subdirectory. Should locate it and display its contents.
        return -1;
    }

    return 0;
}

// Reads size bytes from the file path, from given offset (not yet!) and
// puts them in the buffer.
// TODO: [READ_OFFSET] get to the right offset (navigate the file blocks) to
// read the size It works ok even if you read no more than one block at the time
// but make sure you return the right number of bytes you read.
static int do_read(const char *path, char *buffer, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    printf("--> Trying to read %s, %" PRId64 ", %zu\n", path, offset, size);

    // skip the "/" in the begining
    const char *fn = &path[1];
    // let's figure out the dir entry for the path
    load_directory();
    int di = find_dir_entry(fn);
    if (di < 0)
    {
        // no such file
        printf("    no such file\n");
        return -ENOENT;
    }
    dir_entry *de = index2dir_entry(di);
    unsigned bid = de->first_block;

    char bcache[BLOCK_SIZE];
    
    unsigned int byte_offset = offset % BLOCK_SIZE;
    unsigned int block_offset = offset / BLOCK_SIZE;
    unsigned short *bmap = load_blockmap();

    for(int i = 0; i < block_offset;i++) {
        bid = bmap[bid];
    }

    if(bid == EOF_BLOCK) return 0;
    // reads the block into the cache
    readBlock(bid, bcache);
    // cannot read all maybe?
    size_t rsize = min(size, BLOCK_SIZE - byte_offset);
    memcpy(buffer, bcache + byte_offset, rsize);

    size -= rsize;
    bid = bmap[bid];
    while(size >= BLOCK_SIZE && bid != EOF_BLOCK) {
        readBlock(bid, bcache);
        memcpy(buffer + rsize, bcache, BLOCK_SIZE);
        bid = bmap[bid];
        size -= BLOCK_SIZE;
        rsize += BLOCK_SIZE;
    }
    
    if (size > 0 && bid != EOF_BLOCK) {
        readBlock(bid, bcache);
        memcpy(buffer + rsize, bcache, size);
        rsize += size;
    }
    return rsize;

}

// Writes buffer to file, at given offset. Should extend the file if necessary
// which could mean allocating blocks. Expects to write the full size.
// TODO: [LARGE_WRITE] As is, it does not allow for writes extending over
// several blocks. fix that.
static int do_write(const char *path, const char *buffer, size_t size,
                    off_t offset, struct fuse_file_info *fi)
{
    printf("--> Trying to write %s, %" PRId64 ", %zu\n", path, offset, size);

    // let's figure out the dir entry
    // skip the "/" in the begining
    const char *fn = &path[1];
    // let's figure out the dir entry for the path
    load_directory();
    int di = find_dir_entry(fn);
    if (di < 0)
    { // no such file
        printf("    no such file\n");
        return -ENOENT;
    }
    dir_entry *de = index2dir_entry(di);
    de->modtime = time(NULL);

    // load the block map
    unsigned short *bmap = load_blockmap();

    // do we need to extend the file size?
    if (offset + size > de->size_bytes)
    {
        // file needs to grow
        printf("   file needs to grow by %" PRIu64 " bytes\n",
               offset + size - de->size_bytes);
        // allocate blocks and write it
        if (de->first_block == EOF_BLOCK)
        {
            // this is an empty file! start by allocating a new block
            de->first_block = alloc_block();
            if (de->first_block == EOF_BLOCK)
            {
                printf("   no more free blocks!\n");
                return -ENOSPC;
            }
        }
        // update the size of the file
        de->size_bytes = offset + size;

        // flush back the directory, since the file info changed
        save_directory();
    }
    // first figure out where the write starts (offset in blocks)
    unsigned short blkoffs = offset / BLOCK_SIZE;
    // offset within that block
    unsigned short byteoffs = offset % BLOCK_SIZE;
    // how many bytes to write in this block
    unsigned short crtsize = min(size, BLOCK_SIZE - byteoffs);

    // Depending on how the blocks in a files are organized, blkoffs needs to
    // be mapped to a physical disk block. 3rd block in a contiguous mapped file
    // means the third block in line from the first block. For a file allocated
    // as a linked list of blocks, this is something like this:
    unsigned short crtblk = de->first_block;
    // navigate to the proper block
    while (blkoffs)
    {
        // not yet in the right block.
        if (bmap[crtblk] != EOF_BLOCK)
        {
            // follow the link
            crtblk = bmap[crtblk];
        }
        else
        {
            // past the ends of the blocks, need to allocate a new one
            unsigned short nblk = alloc_block();
            if (nblk == EOF_BLOCK)
            {
                // cannot allocate more, failed
                printf("   out of free blocks!\n");
                return -ENOSPC;
            }
            // add the new block to the list
            bmap[crtblk] = nblk;
            // advance to it
            crtblk = nblk;
        }
        blkoffs--;
    }
    // here crtblk should point to the right block
    printf(" --: start writing at block %u", crtblk);

    char bcache[BLOCK_SIZE];
    // reads the block into the cache
    readBlock(crtblk, bcache);
    // modifies it by writing some bytes from the buffer
    // TODO: [LARGE_WRITE] this only writes one block! needs to be modified to
    // write all, if there are more than 1. Consider a while loop around this.
    // Consider also allocating all new blocks from the start, and then come
    // back to write them.
    memcpy(bcache + byteoffs, buffer, crtsize);
    // write it back
    writeBlock(crtblk, bcache);

    // ... //
    // make sure to update the blockl map
    save_blockmap();

    // how much did we write? lie here to get this running for one block
    // TODO: [LARGE_WRITE] must make sure to write the full size bytes, which
    // might mean several blocks
    return size;
}

// Called when the FS is dismounted
static void do_destroy(void *priv_data)
{
    closeDisk();
    printf("--> FS closed.\n");
}

// needed for "cp" and creating new files
static int do_chmod(const char *path, mode_t mo)
{
    printf("--> Trying to chmod %s, %hu\n", path, mo);
    return 0;
}

// may be needed. for now just used to monitor
static int do_chown(const char *path, uid_t uid, gid_t gid)
{
    printf("--> Trying to chown %s, %u, %u\n", path, uid, gid);
    return 0;
}
static int do_utimens(const char *path, const struct timespec tv[2])
{
    printf("--> Trying to utimens %s\n", path);
    return 0;
}

// Should adjust the length of the given file - shrink or grow accordingly.
// If no such file exists, it should create a new dir entry.
// TODO: [TRUNC_FREE] If the file exists, you need to free its blocks properly,
// or there will be space leaking from the disk.
static int do_truncate(const char *path, off_t offset)
{
    printf("--> Trying to truncate %s, %" PRId64 "\n", path, offset);

    // locate file
    // skip the "/" in the begining
    const char *fn = &path[1];
    // let's figure out the dir entry for the path
    load_directory();
    int di = find_dir_entry(fn);
    if (di < 0)
    {
        // no such file - do nothing?!
        return -ENOENT;
    }

    // file found! must alter both the Directory
    // and free the blocks of the file to the free blocks
    printf("  > file exits. truncate it.");

    dir_entry *de = index2dir_entry(di);
    // de->size_bytes = offset;

    // TODO: [TRUNC_FREE] also free the blocks of this file!
    // load block map
    unsigned short *bmap = load_blockmap();
    unsigned int current_blocks = (de->size_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned int target_blocks = (offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
    unsigned short cur = de->first_block;

    // Shrinking
    if (target_blocks < current_blocks)
    {
        printf("shrinking \n");
        unsigned short prev = EOF_BLOCK;

        for (unsigned i = 0; i < target_blocks; i++)
        {
            prev = cur;
            cur = bmap[cur];
            printf("prev, cur: %d,  %d", prev, cur);
        }

        while (cur != EOF_BLOCK)
        {
            unsigned short next = bmap[cur];
            free_block(cur);
            cur = next;        
        }

        if (prev != EOF_BLOCK)
        {
            bmap[prev] = EOF_BLOCK;
        }
        else
        {
            de->first_block = EOF_BLOCK;
        }

        // Growing
    }
    else if (target_blocks > current_blocks)
    {
        printf("growing \n");
        if (cur == EOF_BLOCK)
        {
            cur = alloc_block();
            de->first_block = cur;
        }
        else
        {
            while (bmap[cur] != EOF_BLOCK)
            {
                cur = bmap[cur];
            }
        }

        for (unsigned i = current_blocks; i < target_blocks; i++)
        {
            unsigned short new_block = alloc_block();
            if (new_block == EOF_BLOCK)
            {
                printf("OOS\n");
                break;
            }
            bmap[cur] = new_block;
            cur = new_block;
        }
        bmap[cur] = EOF_BLOCK;
    }

    de->size_bytes = offset;
    save_blockmap();
    save_directory();

    return 0;
}

// TODO: [RENAME] implement this!
static int do_rename(const char *opath, const char *npath)
{
    printf("--> Trying to rename %s to %s\n", opath, npath);

    // Skip leading /
    const char *old_name = &opath[1];

    int di = find_dir_entry(old_name);
    if (di < 0)
    {
        printf("No such file: %s\n", opath);
        return -ENOENT;
    }

    dir_entry *de = index2dir_entry(di);

    // New path name
    const char *new_name = (npath[0] == '/') ? &npath[1] : npath;

    strncpy(de->name, new_name, FS_NAME_LEN);
    save_directory();

    return 0;
}

// TODO: [REMOVE] implement this!
static int do_unlink(const char *path)
{
    printf("--> Trying to remove %s\n", path);
    const char *fn = &path[1];

    int di = find_dir_entry(fn);
    if (di < 0)
    {
        printf("No such file: %s\n", path);
        return -ENOENT;
    } 

    dir_entry *de = index2dir_entry(di);
    unsigned short cur = de->first_block;
    unsigned short next;
    unsigned short *bmap = load_blockmap();
    while (cur != EOF_BLOCK) {
        next = bmap[cur];
        free_block(cur);
        cur = next;
    }
    de->first_block = EOF_BLOCK;
    de->size_bytes = 0;
    strcpy(de->name, "");
    save_blockmap();
    save_directory();
    return 0; // reports success, but does nothing
}

/*
static int do_mknod(const char *path, mode_t m, dev_t dv) {
  printf("....> Trying to mknod %s mode:%u dev:%u\n", path, m, dv);
  return -1;
}
*/

static int do_create(const char *path, mode_t m, struct fuse_file_info *ffi)
{
    printf("XXXX> Trying to create %s mode:%u\n", path, m);

    // locate file
    // skip the "/" in the begining
    const char *fn = &path[1];
    load_directory();
    int ni = first_empty_dir_entry();
    if (ni < 0)
    { // cannot do anything
        printf("  > no empty entries\n");
        return -ENFILE;
    }
    dir_entry *de = index2dir_entry(ni);

    // paths start with "/", skip that
    strncpy(de->name, fn, FS_NAME_LEN);
    de->mode = m; // S_IFREG | 0644;
    de->size_bytes = 0;
    de->first_block = EOF_BLOCK; // end of file block
    de->creation_time = time(NULL);

    // must save directory changes to disk!
    save_directory();

    return 0;
}
/*
static int do_open(const char *path, struct fuse_file_info *ffi) {
  printf("ZZZZ> Trying to open %s \n", path);
  return -1;
}

static int do_access(const char *path, int ai) {
  printf("--> Trying to access %s %d\n", path, ai);
  return -1;
}
*/

static struct fuse_operations operations = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
    .destroy = do_destroy,
    .write = do_write,
    // just monitoring these for now
    .chown = do_chown,
    .utimens = do_utimens,
    // to make write work
    .chmod = do_chmod,
    // needed for write, also called before open on creation
    .truncate = do_truncate,
    // to implement
    .rename = do_rename,
    .unlink = do_unlink, // implements remove
                         //  .mknod = do_mknod,
    .create = do_create,
    //  .open = do_open,
    //  .access = do_access,
};

int main(int argc, char *argv[])
{
    if (openDisk(DISK_FILE, BLOCK_SIZE * FS_NBLOCKS) < 0)
    {
        perror("open disk failure");
    }
    else
        return fuse_main(argc, argv, &operations, NULL);
}
