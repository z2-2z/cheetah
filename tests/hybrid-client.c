#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzzer-runtime.h"

__AFL_FUZZ_INIT();

void do_libruntime (void) {
    while (spawn_persistent_loop(MAX_ITERATIONS));
}

void do_afl (void) {
    __AFL_INIT();
    while (__AFL_LOOP((unsigned int) MAX_ITERATIONS));
}

int main (int argc, char** argv) {
    if (argc < 2) {
        printf("USAGE: %s { libruntime | afl++ }\n", argv[0]);
        return 1;
    }
    
    if (!strcmp(argv[1], "libruntime")) {
        do_libruntime();
    } else if (!strcmp(argv[1], "afl++")) {
        do_afl();
    } else {
        return 1;
    }
    
    _Exit(0);
}
