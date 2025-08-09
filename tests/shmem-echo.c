#include <stdio.h>

#include "include/fuzzer-runtime.h"

int main (void) {
    while (spawn_persistent_loop(MAX_ITERATIONS)) {
        unsigned char* buf = fuzz_input_all_data();
        size_t len = fuzz_input_len();
        
        if (buf) {
            buf[len] = 0;
        }
        
        printf("Got: %s\n", buf);
    }
    
    printf("End\n");
}
