#ifndef __FORKSERVER_H
#define __FORKSERVER_H

#include <signal.h>

typedef enum {
    MODE_FORKSERVER = 1,
    MODE_PERSISTENT = 2,
} ForkserverMode;

typedef struct {
    int timeout; // in ms
    int signal;
    unsigned char exit_codes[32];
} ForkserverConfig;

typedef enum {
    STATUS_EXIT = 0,
    STATUS_CRASH = 1,
    STATUS_TIMEOUT = 2,
} ForkserverStatus;

typedef enum {
    COMMAND_RUN = 0,
    COMMAND_STOP = 1,
} ForkserverCommand;

extern int started;

int initialize_forkserver (ForkserverMode mode, int pipe_fds[2], ForkserverConfig* config);
ForkserverStatus convert_status (ForkserverConfig* config, int status);

#endif
