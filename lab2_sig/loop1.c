#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void usr2_handler(int signo) {
  if (signo == SIGUSR2) {
    printf("\nReceived SIGUSR2 signal\n");
  }
}

void signal_handler(int signo) {
  if (signo == SIGINT) {
    printf("Process PID: %d\n", getpid());

    struct sigaction sa_usr1;
    sa_usr1.sa_handler = SIG_IGN;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
      printf("sigaction for SIGUSR1");
      return;
    }

    struct sigaction sa_usr2;
    sa_usr2.sa_handler = usr2_handler;
    sigemptyset(&sa_usr2.sa_mask);
    sa_usr2.sa_flags = 0;

    if (sigaction(SIGUSR2, &sa_usr2, NULL) == -1) {
      printf("sigaction for SIGUSR2");
      return;
    }

    for (;;) {}

  }
}

int main() {

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;


    for (;;) {

      if (sigaction(SIGINT, &sa, NULL) == -1) {
        printf("sigaction\n");
      }
        
    }

    return 0;
}