#ifndef KERNEL_MEMORY_LAYOUT_H
#define KERNEL_MEMORY_LAYOUT_H

#include <stdint.h>

#define KERNEL_HEAP_SIZE_MB 8
#define IDENTITY_MAP_SIZE_MB 32

#define MB (1024 * 1024)
#define PAGE_TABLE_SIZE (4 * MB)
#define KERNEL_CODE_START 0x00100000
#define KERNEL_HEAP_START 0x00300000
#define KERNEL_HEAP_END (KERNEL_HEAP_START + (KERNEL_HEAP_SIZE_MB * MB))
#define USER_SPACE_START KERNEL_HEAP_END
#define USER_SPACE_END 0x80000000
#define USER_STACK_BASE 0x80000000
#define USER_STACK_TOP 0xC0000000
#define USER_CODE_BASE 0x08048000
#define USER_HEAP_BASE 0x40000000
#define KERNEL_SPACE_START KERNEL_CODE_START
#define KERNEL_SPACE_END KERNEL_HEAP_END
#define IDENTITY_MAP_END ((IDENTITY_MAP_SIZE_MB) * MB)
#define NUM_KERNEL_PAGE_TABLES (IDENTITY_MAP_END / PAGE_TABLE_SIZE)
#define MAX_PAGE_TABLES 16

_Static_assert(KERNEL_HEAP_END <= IDENTITY_MAP_END,
               "ERROR: Kernel heap exceeds identity-mapped region");

_Static_assert(
    NUM_KERNEL_PAGE_TABLES <= MAX_PAGE_TABLES,
    "ERROR: Too many page tables required - reduce KERNEL_HEAP_SIZE_MB");

_Static_assert(USER_SPACE_START == KERNEL_HEAP_END,
               "ERROR: User space must start immediately after kernel heap");

_Static_assert(KERNEL_SPACE_END == KERNEL_HEAP_END,
               "ERROR: Kernel space end must match heap end");

_Static_assert(KERNEL_HEAP_SIZE_MB >= 4 && KERNEL_HEAP_SIZE_MB <= 32,
               "ERROR: KERNEL_HEAP_SIZE_MB must be between 4 and 32");

#define KERNEL_BASE 0xC0000000
#define KERNEL_STACK_SIZE 0x2000
#define USER_STACK_SIZE 0x100000
#define HEAP_START USER_CODE_BASE

#define HEAP_MIN_SIZE (KERNEL_HEAP_SIZE_MB * MB)
#define HEAP_MAX_SIZE 0x10000000
#define HEAP_ALIGNMENT 0x1000

#endif /* KERNEL_MEMORY_LAYOUT_H */
