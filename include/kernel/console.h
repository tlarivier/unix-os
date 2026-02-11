#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/process.h>

#define CONSOLE_INODE_MAGIC  0xC0C0

int console_init(void);
int console_open_stdio(process_t *proc);
int is_console_fd(int fd);

#endif 
