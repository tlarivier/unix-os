#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

/*
 * Kernel IPC Interface
 * 
 * Internal kernel definitions for Inter-Process Communication
 */

#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>

/*
 * Pipe functions - defined in ipc/pipe.c
 */
void pipe_subsystem_init(void);
int sys_pipe(int pipefd[2]);
int sys_pipe2(int pipefd[2], int flags);
ssize_t pipe_read(int fd, void *buf, size_t count);
ssize_t pipe_write(int fd, const void *buf, size_t count);
int pipe_close(int fd);

/*
 * Syscall handlers for pipes
 */
int32_t sys_pipe_handler(uint32_t pipefd_ptr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5);
int32_t sys_pipe2_handler(uint32_t pipefd_ptr, uint32_t flags, uint32_t u3, uint32_t u4, uint32_t u5);

/*
 * Future IPC implementations (TODO)
 */
// Message queues
// int sys_msgget(key_t key, int msgflg);
// int sys_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
// ssize_t sys_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);

// Shared memory  
// int sys_shmget(key_t key, size_t size, int shmflg);
// void *sys_shmat(int shmid, const void *shmaddr, int shmflg);
// int sys_shmdt(const void *shmaddr);

// Semaphores
// int sys_semget(key_t key, int nsems, int semflg);
// int sys_semop(int semid, struct sembuf *sops, size_t nsops);

#endif /* KERNEL_IPC_H */
