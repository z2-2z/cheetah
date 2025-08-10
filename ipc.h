#ifndef __IPC_H
#define __IPC_H

int ipc_open (void);
void ipc_write (void* buffer, size_t length);
void ipc_read (void* buffer, size_t length);

#endif /* __IPC_H */
