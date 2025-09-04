#ifndef __FUZZER_RUNTIME
#define __FUZZER_RUNTIME

#include <stddef.h>
#include <limits.h>

#define MAX_ITERATIONS ULLONG_MAX

void spawn_forkserver (void);
int spawn_persistent_loop (size_t iterations);

unsigned char* fuzz_input_ptr (void);
size_t fuzz_input_len (void);

#endif /* __FUZZER_RUNTIME */
