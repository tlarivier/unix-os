#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_NAME_MAX 8

typedef struct block_device {
  char name[BLOCK_NAME_MAX];
  uint32_t block_size;
  uint64_t total_blocks;
  void *private_data;
  int (*read_blocks)(struct block_device *bd, uint64_t lba, uint32_t count,
                     void *buf);
  int (*write_blocks)(struct block_device *bd, uint64_t lba, uint32_t count,
                      const void *buf);
  int (*flush)(struct block_device *bd);
} block_device_t;

int block_device_register(block_device_t *bd);
block_device_t *block_device_find(const char *name);
static inline int blk_read(block_device_t *bd, uint64_t lba, uint32_t count,
                           void *buf) {
  return bd->read_blocks(bd, lba, count, buf);
}
static inline int blk_write(block_device_t *bd, uint64_t lba, uint32_t count,
                            const void *buf) {
  return bd->write_blocks(bd, lba, count, buf);
}
static inline int blk_flush(block_device_t *bd) {
  if (bd && bd->flush)
    return bd->flush(bd);
  return 0;
}

#endif
