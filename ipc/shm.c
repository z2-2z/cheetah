#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/shm.h>

#include "../utils.h"
#include "ipc.h"

#define FORKSERVER_SHM_ENV_VAR "__FORKSERVER_SHM"
#define MAX_MESSAGE_SIZE 64

typedef enum {
    OP_NONE,
    OP_READ,
    OP_WRITE,
} IPCOp;

typedef struct {
    sem_t semaphore;
    size_t message_size;
    unsigned char message[MAX_MESSAGE_SIZE];
} Channel;

typedef struct {
    Channel command_channel; // fuzzer -> target
    Channel status_channel;  // target -> fuzzer
    IPCOp last_op;           // last operation the target did
} IPC;

static volatile IPC* shm = NULL;

int ipc_open (void) {
    char* value = getenv(FORKSERVER_SHM_ENV_VAR);
    
    if (!value) {
        return 1;
    }
    
    int id = atoi(value);
    shm = (volatile IPC*) shmat(id, NULL, 0);
    
    if (!shm || shm == (void*) -1) {
        panic(SOURCE_IPC, "Could not attach to shm");
    }
    
    return 0;
}

void ipc_close (void) {
    // Detaching is not necessary for performance reasons
    shm = NULL;
}

static void check_op (IPCOp op) {
    if (shm->last_op != op) {
        shm->last_op = op;
    } else {
        panic(SOURCE_IPC, "Non-alternating operations");
    }
}

int ipc_write (void* buffer, size_t length) {
    check_op(OP_WRITE);
    
    if (length > MAX_MESSAGE_SIZE) {
        panic(SOURCE_IPC, "Message too large for status channel");
    }
    
    shm->status_channel.message_size = length;
    memcpy((void*) &shm->status_channel.message, buffer, length);
    
    if (sem_post((sem_t*) &shm->status_channel.semaphore) == -1) {
        panic(SOURCE_IPC, "Could not write to status channel");
    }
    
    return 0;
}

int ipc_read (void* buffer, size_t length) {
    check_op(OP_READ);
    
    if (sem_wait((sem_t*) &shm->command_channel.semaphore) == -1) {
        panic(SOURCE_IPC, "Could not read from command channel");
    }
    
    if (shm->command_channel.message_size != length) {
        panic(SOURCE_IPC, "Received message over command channel that does not match requested length");
    }
    
    memcpy(buffer, (void*) &shm->command_channel.message, length);
    return 0;
}
