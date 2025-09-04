#include <stdio.h>

#include "include/fuzzer-runtime.h"

int main (void) {
    size_t max_len = fuzz_input_max_len();
    size_t capacity = fuzz_input_capacity();
    printf("Max. Length = %lu | Capacity = %lu\n", max_len, capacity);
    
    while (spawn_persistent_loop(MAX_ITERATIONS)) {
        unsigned char* buf = fuzz_input_ptr();
        size_t len = fuzz_input_len();
        
        if (buf) {
            buf[len] = 0;
        }
        
        printf("Got: %s\n", buf);
    }
    
    printf("End\n");
}
