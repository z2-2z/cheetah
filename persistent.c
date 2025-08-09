#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "forkserver.h"
#include "utils.h"

typedef enum {
    PERSISTENT_INIT,
    PERSISTENT_STOP,
    PERSISTENT_ITER,
} PersistentState;

static size_t iterations;
static int pipe_fds[2];
static ForkserverConfig config;
static struct timespec start_time;
static PersistentState state = PERSISTENT_INIT;

static void check_timeout (int sig) {
    (void) sig;
    
    struct timespec now;
    time_t delta;
    unsigned char c;
    
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    delta = duration_ms(&start_time, &now);
    
    if (delta >= config.timeout) {
        c = STATUS_TIMEOUT;
        write_all(pipe_fds[1], (void*) &c, sizeof(c));
        while (1) {
            raise(SIGKILL);
        }
    }
}

__attribute__((noreturn))
static void handle_crash (int sig) {
    (void) sig;
    unsigned char c = STATUS_CRASH;
    write_all(pipe_fds[1], (void*) &c, sizeof(c));
    while (1) {
        raise(SIGKILL);
    }
}

__attribute__((noreturn))
static void handle_interrupt (int sig) {
    (void) sig;
    unsigned char c = STATUS_EXIT;
    write_all(pipe_fds[1], (void*) &c, sizeof(c));
    while (1) {
        raise(SIGKILL);
    }
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
    time_t secs = config.timeout / 1000;
    suseconds_t usecs = (config.timeout % 1000) * 1000;
    
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
        panic("persistent mode", "Could not set timer");
    }
}

__attribute__((visibility("default")))
int spawn_persistent_loop (size_t iters) {
    unsigned char c;
    int status, err;
    pid_t child = 0;
    
    if (state == PERSISTENT_INIT && started) {
        return 0;
    }
    
    switch (state) {
        case PERSISTENT_INIT: {
            switch (initialize_forkserver(pipe_fds, &config, FORKSERVER_MODE_PERSISTENT)) {
                case 0: break;
                case 1: panic("persistent mode", "Could not initialize forkserver");
                case 2: {
                    state = PERSISTENT_STOP;
                    return 1;
                };
                default: __builtin_unreachable();
            }
            
            err = initialize_persistent_mode();
            if (err) {
                panic("persistent mode", "Could not initialize persistent mode");
            }
            
            iterations = iters;
            started = 1;
            
            while (1) {
                err = read_all(pipe_fds[0], &c, sizeof(c));
                if (err) {
                    break;
                }
                
                if (c == COMMAND_STOP) {
                    break;
                } else if (c == COMMAND_RUN) {
                    child = fork();
        
                    if (child < 0) {
                        panic("persistent mode", "Could not fork");
                    } else if (child == 0) {
                        set_timeout();
                        state = PERSISTENT_ITER;
                        clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
                        return 1;
                    } else {
                        if (waitpid(child, &status, 0) != child) {
                            panic("persistent mode", "Waitpid failed");
                        }
                        
                        if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
                            c = convert_status(&config, status);
                        
                            err = write_all(pipe_fds[1], (void*) &c, sizeof(c));
                            if (err) {
                                break;
                            }
                        }
                    }
                } else {
                    panic("persistent mode", "Invalid command");
                }
            }
            
            // Assume fuzzer has exited
            if (child > 0) {
                kill(child, SIGKILL);
            }
            _Exit(0);
        }
        case PERSISTENT_ITER: {
            if (iterations == 0) {
                state = PERSISTENT_STOP;
                return 0;
            }
            
            c = STATUS_EXIT;
            err = write_all(pipe_fds[1], (void*) &c, sizeof(c));
            if (err) {
                state = PERSISTENT_STOP;
                return 0;
            }
            
            iterations -= 1;
            
            err = read_all(pipe_fds[0], &c, sizeof(c));
            if (err || c == COMMAND_STOP) {
                state = PERSISTENT_STOP;
                return 0;
            } else if (c == COMMAND_RUN) {
                clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
                return 1;
            } else {
                panic("persistent mode", "Invalid command in persistent mode child");
            }
        }
        case PERSISTENT_STOP: {
            return 0;
        }
    }
    
    __builtin_unreachable();
}
