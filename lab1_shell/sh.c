#include "list.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PERM (0644)   /* default permission rw-r--r-- */
#define MAXBUF (512)  /* max length of input line. */
#define MAX_ARG (100) /* max number of cmd line arguments. */
#define READ_END (0)
#define WRITE_END (1)

typedef enum {
  AMPERSAND, /* & */
  NEWLINE,   /* end of line reached. */
  NORMAL,    /* file name or command option. */
  INPUT,     /* input redirection (< file) */
  OUTPUT,    /* output redirection (> file) */
  PIPE,      /* | for instance: ls *.c | wc -l */
  SEMICOLON  /* ; */
} token_type_t;

static char *progname;             /* name of this shell program. */
static char input_buf[MAXBUF];     /* input is placed here. */
static char token_buf[2 * MAXBUF]; /* tokens are placed here. */
static char *input_char;           /* next character to check. */
static char *token;                /* a token such as /bin/ls */

static list_t *path_dir_list; /* list of directories in PATH. */
static int input_fd;          /* for i/o redirection or pipe. */
static int output_fd;         /* for i/o redirection or pipe */
static int pipe_fd[2];

/* fetch_line: read one line from user and put it in input_buf. */
int fetch_line(char *prompt) {
  int c;
  int count;

  input_char = input_buf;
  token = token_buf;

  printf("%s", prompt);
  fflush(stdout);

  count = 0;

  for (;;) {

    c = getchar();

    if (c == EOF)
      return EOF;

    if (count < MAXBUF)
      input_buf[count++] = c;

    if (c == '\n' && count < MAXBUF) {
      input_buf[count] = 0;
      return count;
    }

    if (c == '\n') {
      printf("too long input line\n");
      return fetch_line(prompt);
    }
  }
}

/* end_of_token: true if character c is not part of previous token. */
static bool end_of_token(char c) {
  switch (c) {
  case 0:
  case ' ':
  case '\t':
  case '\n':
  case ';':
  case '|':
  case '&':
  case '<':
  case '>':
    return true;

  default:
    return false;
  }
}

/* gettoken: read one token and let *outptr point to it. */
int gettoken(char **outptr) {
  token_type_t type;

  *outptr = token;

  while (*input_char == ' ' || *input_char == '\t')
    input_char++;

  *token++ = *input_char;

  switch (*input_char++) {
  case '\n':
    type = NEWLINE;
    break;

  case '<':
    type = INPUT;
    break;

  case '>':
    type = OUTPUT;
    break;

  case '&':
    type = AMPERSAND;
    break;

  case '|':
    type = PIPE;
    break;

  default:
    type = NORMAL;

    while (!end_of_token(*input_char))
      *token++ = *input_char++;
  }

  *token++ = 0; /* null-terminate the string. */

  return type;
}

/* error: print error message using formatting string similar to printf. */
void error(char *fmt, ...) {
  va_list ap;

  fprintf(stderr, "%s: error: ", progname);

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  /* print system error code if errno is not zero. */
  if (errno != 0) {
    fprintf(stderr, ": ");
    perror(0);
  } else
    fputc('\n', stderr);
}

int search_path(const char *command, char *resolved_path, size_t size) {
  if (!path_dir_list || !command) {
    errno = EINVAL; // Invalid argument
    return -1;

    void change_directory(char **args) {
      const char *path;
      char current_directory[MAXBUF];
      static char previous_directory[MAXBUF];

      if (strcmp(args[1], "-") == 0) {
        if (previous_directory[0] == '\0') {
          fprintf(stderr, "cd: old dir not found\n");
          return;
        }
        path = previous_directory;
        printf("%s\n", path);
      } else {
        path = args[1];
      }

      if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        perror("getcwd");
        return;
      }

      if (chdir(path) == -1) {
        perror("cd");
        return;
      }

      strncpy(previous_directory, current_directory,
              sizeof(previous_directory) - 1);
      previous_directory[sizeof(previous_directory) - 1] = '\0';
    }

    void check_zombies() {
      pid_t pid;
      int status;

      while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
          printf("Child process %d exited with status %d\n", pid,
                 WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
          printf("Child process %d terminated by signal %d\n", pid,
                 WTERMSIG(status));
        }
      }
    }

    int search_path(const char *command, char *resolved_path, size_t size) {
      if (!path_dir_list || !command) {
        errno = EINVAL; // Invalid argument
        return -1;
      }
      list_t *current = path_dir_list;
      do {
        snprintf(resolved_path, size, "%s/%s", (char *)current->data, command);

        if (access(resolved_path, X_OK) == 0) {
          return 0;
        }
        current = current->succ;
      } while (current != path_dir_list);
      return -1;
    }

    /* run_program: fork and exec a program. */
    void run_program(char **argv, int argc, bool foreground, bool doing_pipe) {
      /* you need to fork, search for the command in argv[0],
       * setup stdin and stdout of the child process, execv it.
       * the parent should sometimes wait and sometimes not wait for
       * the child process (you must figure out when). if foreground
       * is true then basically you should wait but when we are
       * running a command in a pipe such as PROG1 | PROG2 you might
       * not want to wait for each of PROG1 and PROG2...
       *
       * hints:
       *  snprintf is useful for constructing strings.
       *  access is useful for checking wether a path refers to an
       *      executable program.
       *
       *
       */
      char resolved_path[MAXBUF];
      char *command = argv[0];

      if (strchr(command, '/')) {
        strncpy(resolved_path, command, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path - 1)] = '\0';

        if (access(resolved_path, X_OK) != 0) {
          fprintf(stderr, "Error: %s is not executable: %s\n", command,
                  strerror(errno));
          return;
        }
      } else {
        if (search_path(command, resolved_path, sizeof(resolved_path)) < 0) {
          fprintf(stderr, "Error: command %s not found in path\n", command);
          return;
        }
      }

      fprintf(stderr, "Resolved command: %s\n", resolved_path);

      pid_t pid = fork();
      if (pid < 0) {
        fprintf(stderr, "Fork failed");
        return;
      } else if (pid == 0) {
        // child process

        if (input_fd > 0) {
          if (dup2(input_fd, STDIN_FILENO) < 0) {
            perror("dup2 input failed\n");
            exit(EXIT_FAILURE);
          }
          close(input_fd);
        }

        if (output_fd > 0) {
          if (dup2(output_fd, STDOUT_FILENO) < 0) {
            perror("dup2 output failed\n");
          }
          close(output_fd);
        }

        if (doing_pipe && argc > 0) {
          close(STDOUT_FILENO);
          dup(pipe_fd[WRITE_END]);
          close(pipe_fd[READ_END]);
          close(pipe_fd[WRITE_END]);
        }

        execv(resolved_path, argv);
        perror("execv failed");
        exit(EXIT_FAILURE);
      } else {
        // parent process
        int status;
        waitpid(pid, &status, 0);
        fprintf(stderr, "Child process completed with status: %d\n", status);
        if (foreground) {
          int status;
          waitpid(pid, &status, 0);
          fprintf(stderr, "Child process completed with status: %d\n", status);
        } else {
          fprintf(stderr, "Running in the background with PID: %d\n", pid);
        }
        if (doing_pipe) {
          close(pipe_fd[WRITE_END]);
        }
      }
    }

    void parse_line(void) {
      char *argv[MAX_ARG + 1];
      int argc;
      // int pipe_fd[2]; /* 1 for producer and 0 for consumer. */
      token_type_t type;
      bool foreground;
      bool doing_pipe;

      input_fd = 0;
      output_fd = 0;
      argc = 0;

      for (;;) {

        foreground = true;
        doing_pipe = false;

        type = gettoken(&argv[argc]);

        switch (type) {
        case NORMAL:
          argc += 1;
          break;

        case INPUT:
          type = gettoken(&argv[argc]);
          if (type != NORMAL) {
            error("expected file name: but found %s", argv[argc]);
            return;
          }

          input_fd = open(argv[argc], O_RDONLY);

          if (input_fd < 0)
            error("cannot read from %s", argv[argc]);

          break;

        case OUTPUT:
          type = gettoken(&argv[argc]);
          if (type != NORMAL) {
            error("expected file name: but found %s", argv[argc]);
            return;
          }

          output_fd = open(argv[argc], O_CREAT | O_WRONLY, PERM);

          if (output_fd < 0)
            error("cannot write to %s", argv[argc]);
          break;

        case PIPE:
          doing_pipe = true;
          if (pipe(pipe_fd) == -1) {
            error("pipe failed: %s", strerror(errno));
            return;
          }

          // Forks the first command and connects its stdout to the pipe
          run_program(argv, argc, false, true);
          close(pipe_fd[WRITE_END]);
          input_fd = pipe_fd[READ_END];
          argc = 0;

        case AMPERSAND:
          foreground = false;

          /*FALLTHROUGH*/

        case NEWLINE:
        case SEMICOLON:

          if (argc == 0)
            return;

          argv[argc] = NULL;

          if (strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
              fprintf(stderr, "cd: missing arg\n");
            } else {
              change_directory(argv);
            }
          }

          run_program(argv, argc, foreground, doing_pipe);

          input_fd = 0;
          output_fd = 0;
          argc = 0;

          if (type == NEWLINE)
            return;

          break;
        }
      }
    }

    /* init_search_path: make a list of directories to look for programs in. */
    static void init_search_path(void) {
      char *dir_start;
      char *path;
      char *s;
      list_t *p;
      bool proceed;

      path = getenv("PATH");

      /* path may look like "/bin:/usr/bin:/usr/local/bin"
       * and this function makes a list with strings
       * "/bin" "usr/bin" "usr/local/bin"
       *
       */

      dir_start = malloc(1 + strlen(path));
      if (dir_start == NULL) {
        error("out of memory.");
        exit(1);
      }

      strcpy(dir_start, path);

      path_dir_list = NULL;

      if (path == NULL || *path == 0) {
        path_dir_list = new_list("");
        return;
      }

      proceed = true;

      while (proceed) {
        s = dir_start;
        while (*s != ':' && *s != 0)
          s++;
        if (*s == ':')
          *s = 0;
        else
          proceed = false;

        insert_last(&path_dir_list, dir_start);

        dir_start = s + 1;
      }

      p = path_dir_list;

      if (p == NULL)
        return;

#if 0
	do {
		printf("%s\n", (char*)p->data);
		p = p->succ;
	} while (p != path_dir_list);
#endif
    }

    /* main: main program of simple shell. */
    int main(int argc, char **argv) {
      char *prompt = (argc >= 2 && !strncmp(argv[1], "-n", 3)) ? "" : "% ";

      progname = argv[0];

      init_search_path();

      while (fetch_line(prompt) != EOF)
        parse_line();

      return 0;
    }
