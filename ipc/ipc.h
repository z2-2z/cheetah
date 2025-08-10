#ifndef __IPC_H
#define __IPC_H

// Initialize the client-side of the IPC mechanism.
// Return values are:
//   0: success
//   1: error
//   2: No IPC available (running in standalone mode)
int ipc_open (void);

void ipc_close (void);

// Write all bytes in buffer / Read until buffer is full.
// Return value indicates whether the IPC connection is still alive
int ipc_write (void* buffer, size_t length);
int ipc_read (void* buffer, size_t length);

#endif /* __IPC_H */
