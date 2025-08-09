#ifndef __FUZZER_RUNTIME
#define __FUZZER_RUNTIME

#include <stddef.h>
#include <limits.h>

#define MAX_ITERATIONS ULLONG_MAX

void spawn_forkserver (void);
int spawn_persistent_loop (size_t iterations);

unsigned char* fuzz_input_remaining_data (void);
unsigned char* fuzz_input_all_data (void);
size_t fuzz_input_len (void);
unsigned char* fuzz_input_consume (size_t n, size_t* ret_len);

#endif /* __FUZZER_RUNTIME */
