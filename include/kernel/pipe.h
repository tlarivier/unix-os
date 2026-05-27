#ifndef _KERNEL_PIPE_H
#define _KERNEL_PIPE_H

#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

void pipe_subsystem_init(void);
ssize_t pipe_read_by_id(int pipe_id, void *buf, size_t count);
ssize_t pipe_write_by_id(int pipe_id, const void *buf, size_t count);
int pipe_close_by_id(int pipe_id, int is_write_end);
int is_pipe_fd(uint32_t fd, int *pipe_id);

#endif
