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
    abort();
}

unsigned long duration_ms (struct timespec* start, struct timespec* end) {
    long delta_sec = end->tv_sec - start->tv_sec;
    long delta_nsec = end->tv_nsec - start->tv_nsec;
    
    if (delta_nsec < 0) {
        delta_sec--;
        delta_nsec += 1000000000L;
    }
    
    return (unsigned long)delta_sec * 1000ULL + (unsigned long)delta_nsec / 1000000ULL;
}
