#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv []) {
  pid_t pid;
  int status;

  pid = fork();

  if (pid < 0) {
    perror("fork failed\n");
    return 1;
  }

  else if (pid == 0) {
    // child process
    execvp(argv[1], &argv[1]);
    perror("exec failed");
    return 1;
  }
  else {
    // parent process
    if (waitpid(pid, &status, 0) == -1) {
      perror("waitpid");
      return 1;
    }
    if (WIFEXITED(status)) {
      printf("Exit status: %d\n", WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status)) {
      printf("Signal number: %d\n", WTERMSIG(status));
    }
    else {
      printf("boy what the hell\n");
    }
  }

  return 0;

}