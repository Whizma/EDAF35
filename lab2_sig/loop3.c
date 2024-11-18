#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

volatile sig_atomic_t continue_loop = 1;

void alarm_handler(int signo) {
    if (signo == SIGALRM) {
        continue_loop = 0;
    }
}

int main() {
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    alarm(10);

    unsigned long iterations = 0;
    while (continue_loop) {
        iterations++;
    }

    printf("Completed %lu iterations in 10 seconds\n", iterations);

    return 0;
}