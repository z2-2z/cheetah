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
    size_t max_length;
    unsigned char data[];
} FuzzInput;

static volatile FuzzInput* shm = NULL;
static int is_stdin = 0;

static unsigned char* consume_stdin (size_t* final_length, size_t* final_max_length) {
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
    *final_max_length = capacity - sizeof(FuzzInput);
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
        size_t input_len = 0, max_len = 0;
        shm = (volatile FuzzInput*) consume_stdin(&input_len, &max_len);
        shm->length = input_len;
        shm->max_length = max_len;
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
size_t fuzz_input_max_len (void) {
    if (!shm) {
        fuzz_input_initialize();
    }
    
    return shm->max_length;
}

VISIBLE
size_t fuzz_input_capacity (void) {
    if (!shm) {
        fuzz_input_initialize();
    }
    
    size_t total_length = sizeof(FuzzInput) + shm->max_length;
    
    if (total_length % PAGE_SIZE) {
        total_length += PAGE_SIZE - (total_length % PAGE_SIZE);
    }
    
    return total_length;
}
