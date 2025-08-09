#ifndef __UTILS_H
#define __UTILS_H

#include <stddef.h>
#include <time.h>

typedef enum {
    SOURCE_FORKSERVER,
    SOURCE_PERSISTENT,
    SOURCE_FUZZ_INPUT,
} ErrorSource;

__attribute__((noreturn)) void panic (ErrorSource source, const char* message);
int write_all (int fd, void* buf, size_t count);
int read_all (int fd, void* buf, size_t count);
time_t duration_ms (struct timespec* start, struct timespec* end);

#endif
