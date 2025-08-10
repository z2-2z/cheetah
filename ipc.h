#ifndef __IPC_H
#define __IPC_H

int ipc_initialize (void);

// Write all bytes in buffer / Read until buffer is full.
// Return value indicates whether the IPC connection is still alive
int ipc_write (void* buffer, size_t length);
int ipc_read (void* buffer, size_t length);

#endif /* __IPC_H */
