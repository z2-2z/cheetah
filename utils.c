#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "ipc.h"
#include "input.h"

__attribute__((noreturn))
void panic (ErrorSource source, const char* message) {
    const char* source_str = NULL;
    
    switch (source) {
        case SOURCE_FORKSERVER: {
            source_str = "Forkserver";
            break;
        }
        case SOURCE_PERSISTENT: {
            source_str = "Persistent mode";
            break;
        }
        case SOURCE_FUZZ_INPUT: {
            source_str = "Fuzz input";
            break;
        }
        case SOURCE_IPC: {
            source_str = "IPC";
            break;
        }
    }
    
    fprintf(stderr, "%s runtime failure: %s (errno=\"%s\")\n", source_str, message, strerror(errno));
    fflush(stderr);
    
    ipc_cleanup();
    fuzz_input_cleanup();
    while (1) abort();
}

time_t duration_ms (struct timespec* start, struct timespec* end) {
    time_t delta_sec = end->tv_sec - start->tv_sec;
    time_t delta_nsec;
    
    if (delta_sec == 0) {
        delta_nsec = end->tv_nsec - start->tv_nsec;
    } else {
        delta_sec -= 1;
        delta_nsec = end->tv_nsec + (1000000000UL - start->tv_nsec);
    }
    
    while (delta_nsec >= 1000000000L) {
        delta_sec += 1;
        delta_nsec -= 1000000000UL;
    }
    
    return delta_sec * 1000UL + delta_nsec / 1000000UL;
}
