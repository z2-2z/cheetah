#include <stdlib.h>
#include <unistd.h>

#include "../utils.h"
#include "ipc.h"

#define FORKSERVER_FD_ENV_VAR "__FORKSERVER_FD"

static int pipe_fds[2];

int ipc_open (void) {
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

void ipc_close (void) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

int ipc_write (void* buffer, size_t length) {
    size_t total = 0;
    
    do {
        ssize_t r = write(pipe_fds[1], (char*)buffer + total, length - total);
        
        if (r <= 0) {
            return 1;
        } else {
            total += r;
        }
    } while (total < length);
    
    return 0;
}

int ipc_read (void* buffer, size_t length) {
    size_t total = 0;
    
    do {
        ssize_t r = read(pipe_fds[0], (char*)buffer + total, length - total);
        
        if (r <= 0) {
            return 1;
        } else {
            total += r;
        }
    } while (total < length);
    
    return 0;
}
