#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define LOOP_DURATION 10

int main() {
    sigset_t block_set, pending_set;
    struct timespec start, current;
    int signals_received = 0;

    // Block all signals during the loop
    sigfillset(&block_set);

    printf("Process PID: %d\n", getpid());

    // Block signals
    sigprocmask(SIG_BLOCK, &block_set, NULL);
 

    // Get start time
    clock_gettime(CLOCK_REALTIME, &start);


    while (1) {
        // Get current time
        clock_gettime(CLOCK_REALTIME, &current);
  

        if (current.tv_sec - start.tv_sec >= LOOP_DURATION) {
            break;
        }

        sleep(1); 
    }

    // Check for pending signals
    sigpending(&pending_set);   

    // Print signals received
    printf("\nSignals received during execution:\n");
    for (int signo = 1; signo < _NSIG; signo++) {

        // Check if this signal was pending
        if (sigismember(&pending_set, signo)) {
            printf("Signal %d\n", signo);
            signals_received++;
        }
    }

    if (signals_received == 0) {
        printf("No signals were received during the execution.\n");
    }

    // Unblock signals
    sigprocmask(SIG_UNBLOCK, &block_set, NULL);

    return 0;
}