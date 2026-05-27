#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#include <stdint.h>
#include <sys/types.h>

struct dirent {
  uint32_t d_ino;
  uint32_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[256];
};

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

int getdents(int fd, void *buf, size_t count);

#endif
