#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "include/fuzzer-runtime.h"
#include "forkserver.h"
#include "utils.h"
#include "ipc.h"

int started = 0;

int forkserver_handshake (ForkserverMode mode, ForkserverConfig* config) {
    int err = ipc_open();
    if (err) {
        return err;
    }
    
    unsigned int ident = FORKSERVER_MAGIC | (FORKSERVER_VERSION << 8) | mode;
    ipc_write(&ident, sizeof(ident));
    
    ipc_read(config, sizeof(*config));
    
    ident = 1;
    ipc_write(&ident, sizeof(ident));
    
    return 0;
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
    unsigned char c;
    
    if (started) {
        return;
    }
    
    switch (forkserver_handshake(MODE_FORKSERVER, &config)) {
        case 0: break;
        case 1: return;
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
        ipc_read(&c, sizeof(c));
        
        if (c == COMMAND_STOP) {
            break;
        } else if (c == COMMAND_RUN) {
            pid_t child = fork();
        
            if (child < 0) {
                panic(SOURCE_FORKSERVER, "Could not fork");
            } else if (child == 0) {
                return;
            } else {
                c = wait_for_child(&config, child, &signals, &timeout);
                ipc_write(&c, sizeof(c));
            }
        } else {
            panic(SOURCE_FORKSERVER, "Invalid command from fuzzer");
        }
    }
    
    // Assume that fuzzer has been killed
    _Exit(0);
}
