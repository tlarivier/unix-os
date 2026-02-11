#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>

typedef struct pipe_buffer {
    char *data;
    size_t size;
    size_t read_pos;
    size_t write_pos;
    size_t count;
    uint32_t readers;
    uint32_t writers;
    uint32_t flags;
    spinlock_t lock;  /* Protects concurrent access to pipe buffer */
} pipe_buffer_t;

typedef struct pipe_fd {
    pipe_buffer_t *pipe;
    int mode;
    int fd;
} pipe_fd_t;

void pipe_subsystem_init(void);
int sys_pipe(int pipefd[2]);
int sys_pipe2(int pipefd[2], int flags);

/* Old API  */
ssize_t pipe_read(int fd, void *buf, size_t count);
ssize_t pipe_write(int fd, const void *buf, size_t count);
int pipe_close(int fd);

/* New API  */
ssize_t pipe_read_by_id(int pipe_id, void *buf, size_t count);
ssize_t pipe_write_by_id(int pipe_id, const void *buf, size_t count);
int pipe_close_by_id(int pipe_id, int is_write_end);

#endif
