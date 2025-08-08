#ifndef __UTILS_H
#define __UTILS_H

#include <stddef.h>
#include <time.h>

__attribute__((noreturn)) void panic (const char* mode, const char* message);
int write_all (int fd, void* buf, size_t count);
int read_all (int fd, void* buf, size_t count);
time_t duration_ms (struct timespec* start, struct timespec* end);

#endif
