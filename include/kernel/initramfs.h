#ifndef KERNEL_INITRAMFS_H
#define KERNEL_INITRAMFS_H

#include <stdint.h>
#include <stddef.h>

int initramfs_load(const void *data, size_t size);
int initramfs_init(void);

#endif 
