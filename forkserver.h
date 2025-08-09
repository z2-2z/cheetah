#ifndef __FORKSERVER_H
#define __FORKSERVER_H

#include <signal.h>

#define FORKSERVER_MODE_FORKSERVER 1
#define FORKSERVER_MODE_PERSISTENT 2

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

int initialize_forkserver (int pipe_fds[2], ForkserverConfig* config, unsigned int mode);
ForkserverStatus convert_status (ForkserverConfig* config, int status);

#endif
