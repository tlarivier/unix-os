#ifndef KERNEL_MULTIBOOT2_H
#define KERNEL_MULTIBOOT2_H

#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289
#define MB2_TAG_END 0

void multiboot_parse(uint32_t magic, uint32_t info_addr);

#endif
