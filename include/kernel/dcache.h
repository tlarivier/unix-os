#ifndef KERNEL_DCACHE_H
#define KERNEL_DCACHE_H

#include <stddef.h>
#include <stdint.h>

#define DCACHE_MAX_ENTRIES 32
#define DCACHE_MAX_PATH_LEN 63

typedef struct dentry {
  char path[DCACHE_MAX_PATH_LEN + 1];
  uint32_t inode;
} dentry_t;

void dcache_init(void);

void dcache_invalidate(const char *path);

#endif
