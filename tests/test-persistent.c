#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "include/fuzzer-runtime.h"

int main (void) {
    while (spawn_persistent_loop(2)) {
        unsigned char* fuzz_input = fuzz_input_data();
        size_t len = fuzz_input_len();
        fuzz_input[len] = 0;
        
        if (!strcmp((char*) fuzz_input, "leak")) {
            malloc(1);
            break;
        } else if (!strcmp((char*) fuzz_input, "timeout")) {
            sleep(10);
        } else if (!strcmp((char*) fuzz_input, "uaf")) {
            char* buf = malloc(16);
            free(buf);
            *buf = 0;
        } else if (!strcmp((char*) fuzz_input, "ub")) {
            printf("%d\n", 0x7FFFFFFF + (int)len);
        } else if (!strcmp((char*) fuzz_input, "null")) {
            char* x = NULL;
            *x = 13;
        } else if (!strcmp((char*) fuzz_input, "trap")) {
            __builtin_trap();
        } else {
            printf("Doing nothing: %s\n", fuzz_input);
        }
    }
    
    printf("Quitting\n");
    
    return 0;
}
