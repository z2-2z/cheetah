#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "fuzzer-runtime.h"
#include "forkserver.h"
#include "utils.h"
#include "ipc.h"
#include "input.h"

typedef enum {
    PERSISTENT_INIT,
    PERSISTENT_STOP,
    PERSISTENT_ITER,
} PersistentState;

static size_t iterations;
static ForkserverConfig config;
static struct timespec start_time;
static PersistentState state = PERSISTENT_INIT;

static void check_timeout (int sig) {
    (void) sig;
    struct timespec now;
    
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    unsigned long delta = duration_ms(&start_time, &now);
    
    if (delta >= config.timeout - 100) {
        ipc_send_status(STATUS_TIMEOUT);
        while (1) raise(SIGKILL);
    }
}

__attribute__((noreturn))
static void handle_crash (int sig) {
    (void) sig;
    ipc_send_status(STATUS_CRASH);
    while (1) raise(SIGKILL);
}

__attribute__((noreturn))
static void handle_interrupt (int sig) {
    (void) sig;
    ipc_send_status(STATUS_EXIT);
    while (1) raise(SIGKILL);
}

static int initialize_persistent_mode (void) {
    sigset_t signals;
    struct sigaction action;
    
    if (sigfillset(&signals) == -1) {
        return 1;
    }
    
    action = (struct sigaction) {
        .sa_handler = check_timeout,
        .sa_mask = signals,
        .sa_flags = 0,
        .sa_restorer = NULL,
    };
    if (sigaction(SIGALRM, &action, NULL) == -1) {
        return 1;
    }
    
    action = (struct sigaction) {
        .sa_handler = handle_crash,
        .sa_mask = signals,
        .sa_flags = 0,
        .sa_restorer = NULL,
    };
    if (sigaction(SIGBUS, &action, NULL) == -1 ||
        sigaction(SIGABRT, &action, NULL) == -1 ||
        sigaction(SIGILL, &action, NULL) == -1 ||
        sigaction(SIGFPE, &action, NULL) == -1 ||
        sigaction(SIGSEGV, &action, NULL) == -1 ||
        sigaction(SIGTRAP, &action, NULL) == -1
    ) {
        return 1;
    }
    
    action = (struct sigaction) {
        .sa_handler = handle_interrupt,
        .sa_mask = signals,
        .sa_flags = 0,
        .sa_restorer = NULL,
    };
    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1
    ) {
        return 1;
    }
    
    if (sigemptyset(&signals) == -1 ||
        sigaddset(&signals, SIGALRM) == -1 ||
        sigaddset(&signals, SIGBUS) == -1 ||
        sigaddset(&signals, SIGABRT) == -1 ||
        sigaddset(&signals, SIGILL) == -1 ||
        sigaddset(&signals, SIGFPE) == -1 ||
        sigaddset(&signals, SIGSEGV) == -1 ||
        sigaddset(&signals, SIGTRAP) == -1 ||
        sigaddset(&signals, SIGINT) == -1 ||
        sigaddset(&signals, SIGTERM) == -1 ||
        sigprocmask(SIG_UNBLOCK, &signals, NULL) == -1
    ) {
        return 1;
    }
    
    return 0;
}

static void set_timeout (void) {
    // Disable timeout
    if (config.timeout == 0) {
        return;
    }
    
    time_t secs = config.timeout / 1000;
    suseconds_t usecs = (config.timeout % 1000) * 1000;
    
    // Minimum interval of 1 second
    if (secs == 0) {
        secs = 1;
        usecs = 0;
    }
    
    struct itimerval interval = (struct itimerval) {
        .it_interval = (struct timeval) {
            .tv_sec = secs,
            .tv_usec = usecs,
        },
        .it_value = (struct timeval) {
            .tv_sec = secs,
            .tv_usec = usecs,
        },
    };
    
    if (setitimer(ITIMER_REAL, &interval, NULL) == -1) {
        panic(SOURCE_PERSISTENT, "Could not set timer");
    }
}

VISIBLE
int spawn_persistent_loop (size_t iters) {
    int status;
    pid_t child = -1;
    
    if (!iters || (started && state == PERSISTENT_INIT)) {
        return 0;
    }
    
    switch (state) {
        case PERSISTENT_INIT: {
            if (forkserver_handshake(MODE_PERSISTENT, &config)) {
                state = PERSISTENT_STOP;
                return 1;
            }
            
            if (initialize_persistent_mode()) {
                panic(SOURCE_PERSISTENT, "Could not initialize persistent mode");
            }
            
            iterations = iters;
            started = 1;
            
            while (1) {
                switch (ipc_recv_command()) {
                    case COMMAND_STOP: {
                        if (child > 0) {
                            kill(child, SIGKILL);
                            waitpid(child, NULL, WNOHANG);
                        }
                        ipc_cleanup();
                        fuzz_input_cleanup();
                        _Exit(0);
                    }
                    case COMMAND_RUN: {
                        child = fork();
                        
                        if (child < 0) {
                            panic(SOURCE_PERSISTENT, "Could not fork");
                        } else if (child == 0) {
                            state = PERSISTENT_ITER;
                            iterations -= 1;
                            if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time) == -1) {
                                panic(SOURCE_PERSISTENT, "Could not get start time");
                            }
                            set_timeout();
                            return 1;
                        } else {
                            if (waitpid(child, &status, 0) != child) {
                                panic(SOURCE_PERSISTENT, "Waitpid failed");
                            }
                            
                            if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
                                ipc_send_status(convert_status(&config, status));
                            }
                            
                            child = -1;
                        }
                        
                        break;
                    }
                    default: panic(SOURCE_PERSISTENT, "Invalid command in parent");
                }
            }
        }
        case PERSISTENT_ITER: {
            if (iterations == 0) {
                state = PERSISTENT_STOP;
                return 0;
            }
            
            ipc_send_status(STATUS_EXIT);
            
            iterations -= 1;
            
            switch (ipc_recv_command()) {
                case COMMAND_STOP: {
                    state = PERSISTENT_STOP;
                    return 0;
                }
                case COMMAND_RUN: {
                    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time) == -1) {
                        panic(SOURCE_PERSISTENT, "Could not get start time");
                    }
                    return 1;
                }
                default: panic(SOURCE_PERSISTENT, "Invalid command in child");
            }
        }
        case PERSISTENT_STOP: {
            return 0;
        }
    }
    
    __builtin_unreachable();
}
