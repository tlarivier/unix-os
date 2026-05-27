#ifndef KERNEL_VIRTIO_BLK_H
#define KERNEL_VIRTIO_BLK_H

#include <stdint.h>

int virtio_blk_init(void);
uint64_t virtio_blk_capacity(void);

#endif
