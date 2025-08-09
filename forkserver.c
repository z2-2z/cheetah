#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "include/fuzzer-runtime.h"
#include "forkserver.h"
#include "utils.h"

int started = 0;

int initialize_forkserver (ForkserverMode mode, int pipe_fds[2], ForkserverConfig* config) {
    /* Get pipe fd */
    char* value = getenv(FORKSERVER_FD_ENV_VAR);
    
    if (!value) {
        // Assume the binary is run standalone
        return 2;
    }
    
    int fd = atoi(value);
    
    if (fd < 3 || fd == 198) {
        return 1;
    }
    
    pipe_fds[0] = fd;
    pipe_fds[1] = fd + 1;
    
    /* Do the handshake */
    unsigned int ident = FORKSERVER_MAGIC | (FORKSERVER_VERSION << 8) | mode;
    int err = write_all(pipe_fds[1], (void*) &ident, sizeof(ident));
    if (err) {
        return err;
    }
    
    return read_all(pipe_fds[0], (void*) config, sizeof(*config));
}

ForkserverStatus convert_status (ForkserverConfig* config, int status) {
    if (WIFEXITED(status)) {
        size_t code = (unsigned char) WEXITSTATUS(status);
        unsigned char bitmask = config->exit_codes[code / 8];
        unsigned char bittest = 1 << (code % 8);
        
        if (bitmask & bittest) {
            return STATUS_CRASH;
        } else {
            return STATUS_EXIT;
        }
    } else if (WIFSIGNALED(status)) {
        return STATUS_CRASH;
    } else {
        panic(SOURCE_FORKSERVER, "Invalid status from waitpid");
    }
}

static unsigned char wait_for_child (ForkserverConfig* config, pid_t child, sigset_t* signals, struct timespec* timeout) {
    int status = 0;
    errno = 0;
    int r = sigtimedwait(signals, NULL, timeout);
    
    if (r == -1) {
        if (errno == EAGAIN) {
            // No error handling because the child could have exited in the mean-time
            kill(child, config->signal);
            int old_signal = config->signal;
            config->signal = SIGKILL;
            wait_for_child(config, child, signals, timeout);
            config->signal = old_signal;
            return STATUS_TIMEOUT;
        } else {
            panic(SOURCE_FORKSERVER, "Sigtimedwait failed");
        }
    } else if (r == SIGCHLD) {
        if (waitpid(child, &status, 0) != child) {
            panic(SOURCE_FORKSERVER, "Waitpid for SIGCHLD failed");
        }
        return convert_status(config, status);
    } else {
        panic(SOURCE_FORKSERVER, "Invalid return code from sigtimedwait");
    }
}

__attribute__((visibility("default")))
void spawn_forkserver (void) {
    ForkserverConfig config;
    sigset_t signals;
    struct timespec timeout;
    int err, pipe_fds[2];
    unsigned char c;
    
    if (started) {
        return;
    }
    
    switch (initialize_forkserver(MODE_FORKSERVER, pipe_fds, &config)) {
        case 0: break;
        case 1: panic(SOURCE_FORKSERVER, "Could not initialize forkserver");
        case 2: return;
        default: __builtin_unreachable();
    }
    
    started = 1;
    
    if (sigemptyset(&signals) == -1 ||
        sigaddset(&signals, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, &signals, NULL) == -1
    ) {
        panic(SOURCE_FORKSERVER, "Could not initialize forkserver");
    }
    
    timeout = (struct timespec) {
        .tv_sec = config.timeout / 1000,
        .tv_nsec = (config.timeout % 1000) * 1000 * 1000,
    };
    
    while (1) {
        err = read_all(pipe_fds[0], &c, sizeof(c));
        if (err) {
            break;
        }
        
        if (c == COMMAND_STOP) {
            break;
        } else if (c == COMMAND_RUN) {
            pid_t child = fork();
        
            if (child < 0) {
                panic(SOURCE_FORKSERVER, "Could not fork");
            } else if (child == 0) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                return;
            } else {
                c = wait_for_child(&config, child, &signals, &timeout);
                
                err = write_all(pipe_fds[1], (void*) &c, sizeof(c));
                if (err) {
                    break;
                }
            }
        } else {
            panic(SOURCE_FORKSERVER, "Invalid command from fuzzer");
        }
    }
    
    // Assume that fuzzer has been killed
    _Exit(0);
}
