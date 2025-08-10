#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

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
    
    fprintf(stderr, "%s runtime failure: %s\n", source_str, message);
    fflush(stderr);
    while (1) abort();
}

int write_all (int fd, void* buf, size_t count) {
    size_t total = 0;
    
    do {
        ssize_t r = write(fd, (char*)buf + total, count - total);
        
        if (r <= 0) {
            return 1;
        } else {
            total += r;
        }
    } while (total < count);
    
    return 0;
}

int read_all (int fd, void* buf, size_t count) {
    size_t total = 0;
    
    do {
        ssize_t r = read(fd, (char*)buf + total, count - total);
        
        if (r <= 0) {
            return 1;
        } else {
            total += r;
        }
    } while (total < count);
    
    return 0;
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
