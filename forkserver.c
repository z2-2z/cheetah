#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "forkserver.h"
#include "utils.h"

#define FORKSERVER_FD_ENV_VAR "__FORKSERVER_FD"

#define FORKSERVER_MAGIC_MASK   0xFFFF0000
#define FORKSERVER_VERSION_MASK 0x0000FF00
#define FORKSERVER_MODE_MASK    0x000000FF
#define FORKSERVER_MAGIC   0xDEAD0000
#define FORKSERVER_VERSION 0x0100

int started = 0;

int initialize_forkserver (int pipe_fds[2]) {
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
    
    return 0;
}

int forkserver_handshake (int pipe[2], ForkserverConfig* config, unsigned int mode) {
    int err;
    unsigned int ident = FORKSERVER_MAGIC | FORKSERVER_VERSION | mode;
    
    err = write_all(pipe[1], (void*) &ident, sizeof(ident));
    if (err) {
        return err;
    }
    
    return read_all(pipe[0], (void*) config, sizeof(*config));
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
        panic("forkserver", "Invalid status from waitpid");
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
            panic("forkserver", "Sigtimedwait failed");
        }
    } else if (r == SIGCHLD) {
        if (waitpid(child, &status, 0) != child) {
            panic("forkserver", "Waitpid for SIGCHLD failed");
        }
        return convert_status(config, status);
    } else {
        panic("forkserver", "Invalid return code from sigtimedwait");
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
    
    // Create signal set for timedwait
    if (sigemptyset(&signals) == -1 ||
        sigaddset(&signals, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, &signals, NULL) == -1
    ) {
        panic("forkserver", "Could not initialize forkserver");
    }
    
    switch (initialize_forkserver(pipe_fds)) {
        case 0: break;
        case 1: panic("forkserver", "Could not initialize forkserver");
        case 2: return;
        default: __builtin_unreachable();
    }
    
    started = 1;
    
    err = forkserver_handshake(pipe_fds, &config, FORKSERVER_MODE_FORKSERVER);
    if (err) {
        panic("forkserver", "Could not do forkserver handshake");
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
                panic("forkserver", "Could not fork");
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
            panic("forkserver", "Invalid command from fuzzer");
        }
    }
    
    // Assume that fuzzer has been killed
    _Exit(0);
}
