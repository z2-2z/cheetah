#ifndef __UTILS_H
#define __UTILS_H

#include <stddef.h>
#include <time.h>

#define VISIBLE __attribute__((visibility("default")))

typedef enum {
    SOURCE_FORKSERVER,
    SOURCE_PERSISTENT,
    SOURCE_FUZZ_INPUT,
    SOURCE_IPC,
} ErrorSource;

__attribute__((noreturn)) void panic (ErrorSource source, const char* message);
unsigned long duration_ms (struct timespec* start, struct timespec* end);

#endif
