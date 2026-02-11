#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#include <sys/types.h>

struct dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[1];  /* Variable length, matches kernel linux_dirent */
} __attribute__((packed));

int getdents(int fd, void *buf, size_t count);

#endif 
