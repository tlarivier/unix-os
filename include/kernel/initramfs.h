#ifndef KERNEL_INITRAMFS_H
#define KERNEL_INITRAMFS_H

#include <stddef.h>
#include <stdint.h>

int initramfs_init(void);

int install_userspace_binaries(void);

#endif
