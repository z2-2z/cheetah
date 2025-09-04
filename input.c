#define _GNU_SOURCE
#define __USE_GNU
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include "include/fuzzer-runtime.h"
#include "utils.h"

#define FUZZ_INPUT_SHM_ENV_VAR "__FUZZ_INPUT_SHM"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

typedef struct {
    size_t length;
    size_t capacity;
    unsigned char data[];
} FuzzInput;

static volatile FuzzInput* shm = NULL;
static int is_stdin = 0;

static unsigned char* consume_stdin (size_t* final_length, size_t* final_capacity) {
    size_t length = sizeof(FuzzInput);
    size_t capacity = PAGE_SIZE;
    unsigned char* buffer = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (!buffer || buffer == (void*) -1) {
        panic(SOURCE_FUZZ_INPUT, "Could not mmap");
    }
    
    while (1) {
        size_t free = capacity - length;
        ssize_t r = read(0, &buffer[length], free);
        
        if (r < 0) {
            panic(SOURCE_FUZZ_INPUT, "Cannot read from stdin");
        } else if ((size_t) r < free) {
            length += r;
            break;
        } else {
            length += r;
            capacity += PAGE_SIZE;
            buffer = mremap(buffer, length, capacity, MREMAP_MAYMOVE);
            
            if (!buffer || buffer == (void*) -1) {
                panic(SOURCE_FUZZ_INPUT, "Could not mremap");
            }
        }
    }
    
    *final_length = length - sizeof(FuzzInput);
    *final_capacity = capacity - sizeof(FuzzInput);
    return buffer;
}

static void fuzz_input_initialize (void) {
    char* value = getenv(FUZZ_INPUT_SHM_ENV_VAR);
    
    if (value) {
        /* Use shm as input */
        int id = atoi(value);
        shm = (volatile FuzzInput*) shmat(id, NULL, 0);
        if (!shm || shm == (void*) -1) {
            panic(SOURCE_FUZZ_INPUT, "Could not attach to shm");
        }
    } else {
        /* Use stdin as input */
        size_t input_len = 0, capacity = 0;
        shm = (volatile FuzzInput*) consume_stdin(&input_len, &capacity);
        shm->length = input_len;
        shm->capacity = capacity;
        is_stdin = 1;
    }
}

void fuzz_input_cleanup (void) {
    if (shm && !is_stdin) {
        shmdt((void*) shm);
        shm = NULL;
    }
}

VISIBLE
unsigned char* fuzz_input_ptr (void) {
    if (!shm) {
        fuzz_input_initialize();
    }
    
    return (unsigned char*) &shm->data[0];
}

VISIBLE
size_t fuzz_input_len (void) {
    if (!shm) {
        fuzz_input_initialize();
    }
    
    return shm->length;
}

VISIBLE
size_t fuzz_input_capacity (void) {
    if (!shm) {
        fuzz_input_initialize();
    }
    
    return shm->capacity;
}
