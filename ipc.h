#ifndef __IPC_H
#define __IPC_H

int ipc_init (void);
void ipc_send_all (void* buffer, size_t length);
void ipc_recv_all (void* buffer, size_t length);
unsigned char ipc_recv_command (void);
void ipc_send_status (unsigned char status);
void ipc_cleanup (void);

#endif /* __IPC_H */
