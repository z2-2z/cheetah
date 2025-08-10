/*
The runtime only works when the following env variables are set:
    LSAN_OPTIONS=exitcode=23
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
and the target has been compiled with
    -fsanitize=undefined,address -fsanitize-trap=all
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <time.h>
#include <sys/wait.h>
#include <string.h>

#include "forkserver.h"
#include "include/fuzzer-runtime.h"

#define FORKSERVER_FD_ENV_VAR "__FORKSERVER_FD"
#define ITERATIONS 1

void setup_pipes (int final_pipe[2]) {
    char buffer[128];
    int pipe_forward[2];
    int pipe_backward[2];
    
    assert(pipe(pipe_forward) == 0);
    assert(pipe(pipe_backward) == 0);
    
    int start_pipe = dup(pipe_forward[0]);
    assert(start_pipe != -1);
    assert(dup2(pipe_backward[1], start_pipe + 1) == start_pipe + 1);
    
    snprintf(buffer, sizeof(buffer), "%d", start_pipe);
    setenv(FORKSERVER_FD_ENV_VAR, buffer, 1);
    
    close(pipe_forward[0]);
    final_pipe[1] = pipe_forward[1];
    final_pipe[0] = pipe_backward[0];
    close(pipe_backward[1]);
}

__attribute__((noreturn))
void run_child (char* value) {    
    while (spawn_persistent_loop(ITERATIONS)) {
        if (value && !strcmp(value, "leak")) {
            malloc(1);
        } else if (value && !strcmp(value, "timeout")) {
            sleep(9999999);
        } else if (value && !strcmp(value, "uaf")) {
            char* buf = malloc(16);
            free(buf);
            *buf = 0;
        } else if (value && !strcmp(value, "ub")) {
            printf("%d\n", 0x7FFFFFFF + (int)(long)value);
        } else if (value && !strcmp(value, "null")) {
            char* x = NULL;
            *x = 13;
        } else if (value && !strcmp(value, "trap")) {
            __builtin_trap();
        }
        
        value = NULL;
    }
    
    exit(0);
}

void crash_on_status (ForkserverConfig* config, int status) {
    unsigned char idx = status / 8;
    unsigned char bit = 1 << (status % 8);
    config->exit_codes[idx] |= bit;
}

int main (int argc, char** argv) {
    char* action = NULL;
    if (argc > 1) {
        action = argv[1];
    }
    
    ForkserverConfig config = {
        .timeout = 5000,
        .signal = SIGKILL,
        .exit_codes = {0},
    };
    int pipefds[2];
    char c = COMMAND_RUN, s;

    crash_on_status(&config, 23);
    setup_pipes(pipefds);
    
    pid_t child = fork();
    
    switch (child) {
        case -1: return 1;
        case 0: run_child(action);
        default: {
            unsigned int version;
            
            assert(read(pipefds[0], (void*) &version, sizeof(version)) == sizeof(version));
            assert((version & 0xFFFF0000) == FORKSERVER_MAGIC);
            assert((version & 0x0000FF00) == FORKSERVER_VERSION << 8);
            assert((version & 0x000000FF) == MODE_PERSISTENT);
            
            assert(write(pipefds[1], (void*) &config, sizeof(config)) == sizeof(config));
            
            for (size_t i = 0; i < ITERATIONS; ++i) {
                assert(write(pipefds[1], &c, sizeof(c)) == sizeof(c));
                assert(read(pipefds[0], &s, sizeof(s)) == sizeof(c));
                printf("Child status = %d\n", s);
            }
        }
    }
    
    c = COMMAND_STOP;
    assert(write(pipefds[1], &c, sizeof(c)) == sizeof(c));
    assert(waitpid(child, NULL, 0) == child);
    
    return 0;
}
