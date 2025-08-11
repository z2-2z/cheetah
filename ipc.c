#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <errno.h>

#include "utils.h"
#include "ipc.h"

#define FORKSERVER_SHM_ENV_VAR "__FORKSERVER_SHM"
#define MAX_MESSAGE_SIZE 64

typedef enum {
    OP_NONE,
    OP_READ,
    OP_WRITE,
} IpcOp;

typedef struct {
    sem_t semaphore;
    size_t message_size;
    unsigned char message[MAX_MESSAGE_SIZE];
} Channel;

typedef struct {
    Channel command_channel; // fuzzer -> target
    Channel status_channel;  // target -> fuzzer
    IpcOp last_op;           // last operation the target did
} Ipc;

static volatile Ipc* shm = NULL;

int ipc_init (void) {
    char* value = getenv(FORKSERVER_SHM_ENV_VAR);
    
    if (!value) {
        return 1;
    }
    
    int id = atoi(value);
    shm = (volatile Ipc*) shmat(id, NULL, 0);
    
    if (!shm || shm == (void*) -1) {
        panic(SOURCE_IPC, "Could not attach to shm");
    }
    
    return 0;
}

static void debug_check_op (IpcOp op) {
#ifdef DEBUG
    if (shm->last_op != op) {
        shm->last_op = op;
    } else {
        panic(SOURCE_IPC, "Non-alternating operations");
    }
#else
    (void) op;
#endif
}

void ipc_write (void* buffer, size_t length) {
    debug_check_op(OP_WRITE);
    
    if (length > MAX_MESSAGE_SIZE) {
        panic(SOURCE_IPC, "Message too large for status channel");
    }
    
    shm->status_channel.message_size = length;
    memcpy((void*) &shm->status_channel.message, buffer, length);
    
    while (sem_post((sem_t*) &shm->status_channel.semaphore) == -1) {
        if (errno != EINTR) {
            panic(SOURCE_IPC, "Could not write to status channel");
        }
    }
}

void ipc_read (void* buffer, size_t length) {
    debug_check_op(OP_READ);
    
    while (sem_wait((sem_t*) &shm->command_channel.semaphore) == -1) {
        if (errno != EINTR) {
            panic(SOURCE_IPC, "Could not read from command channel");
        }
    }
    
    if (shm->command_channel.message_size != length) {
        panic(SOURCE_IPC, "Received message over command channel that does not match requested length");
    }
    
    memcpy(buffer, (void*) &shm->command_channel.message, length);
}

unsigned char ipc_recv_command (void) {
    debug_check_op(OP_READ);
    
    while (sem_wait((sem_t*) &shm->command_channel.semaphore) == -1) {
        if (errno != EINTR) {
            panic(SOURCE_IPC, "Could not read from command channel");
        }
    }
    
#ifdef DEBUG
    if (shm->command_channel.message_size != 1) {
        panic(SOURCE_IPC, "Received command with invalid length");
    }
#endif
    
    return shm->command_channel.message[0];
}

void ipc_send_status (unsigned char status) {
    debug_check_op(OP_WRITE);
    
    // Length = 1 is already set by last message of handshake so we don't
    // need to set it anymore
    shm->status_channel.message[0] = status;
    
    while (sem_post((sem_t*) &shm->status_channel.semaphore) == -1) {
        if (errno != EINTR) {
            panic(SOURCE_IPC, "Could not write to status channel");
        }
    }
}
