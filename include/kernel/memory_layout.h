/**
 * memory_layout.h - Centralized Memory Layout Configuration
 * 
 * SINGLE SOURCE OF TRUTH for all memory layout constants.
 * Change KERNEL_HEAP_SIZE_MB to adjust heap size - all other values auto-calculated.
 * 
 * Architecture: x86 32-bit with identity mapping
 * - 0x00000000 - 0x00100000 (0-1MB)   : BIOS, VGA, Boot
 * - 0x00100000 - 0x00200000 (1-2MB)   : Kernel code/data
 * - 0x00200000 - KERNEL_HEAP_END      : Kernel heap (configurable)
 * - KERNEL_HEAP_END - 0x80000000      : User space
 * - 0x80000000 - 0xC0000000 (2GB-3GB) : User stack region
 * - 0xC0000000 - 0xFFFFFFFF (3GB-4GB) : Kernel virtual space
 */

#ifndef KERNEL_MEMORY_LAYOUT_H
#define KERNEL_MEMORY_LAYOUT_H

#include <stdint.h>

/* ============================================================================
 * PRIMARY CONFIGURATION - Change this to adjust heap size
 * ============================================================================ */

#define KERNEL_HEAP_SIZE_MB     8   /* Heap size in MB (8MB stable, use page allocation for large files) */

/* ============================================================================
 * DERIVED CONSTANTS - Automatically calculated, do not modify
 * ============================================================================ */

#define MB                      (1024 * 1024)
#define PAGE_SIZE               4096
#define PAGE_TABLE_SIZE         (4 * MB)    /* Each page table maps 4MB */

/* Kernel Memory Layout */
#define KERNEL_CODE_START       0x00100000  /* 1MB - where kernel loads */
#define KERNEL_HEAP_START       0x00200000  /* 2MB - heap starts after kernel */
#define KERNEL_HEAP_END         (KERNEL_HEAP_START + (KERNEL_HEAP_SIZE_MB * MB))

/* User Space Layout */
#define USER_SPACE_START        KERNEL_HEAP_END
#define USER_SPACE_END          0x80000000  /* 2GB */
#define USER_STACK_BASE         0x80000000
#define USER_STACK_TOP          0xC0000000  /* 3GB */
#define USER_CODE_BASE          0x08048000  /* Standard ELF load address */
#define USER_HEAP_BASE          0x40000000  /* 1GB */

/* Kernel Space Boundaries */
#define KERNEL_SPACE_START      KERNEL_CODE_START
#define KERNEL_SPACE_END        KERNEL_HEAP_END

/* Identity Mapping Configuration */
/* Must be large enough to cover both heap AND initramfs CPIO data in memory */
/* Round up to next 4MB boundary to ensure full page table coverage */
#define IDENTITY_MAP_END        (((KERNEL_HEAP_END + PAGE_TABLE_SIZE - 1) / PAGE_TABLE_SIZE) * PAGE_TABLE_SIZE)

/* Page Table Configuration */
#define NUM_KERNEL_PAGE_TABLES  (IDENTITY_MAP_END / PAGE_TABLE_SIZE)
#define MAX_PAGE_TABLES         16  /* Safety limit: 64MB max identity map */

/* ============================================================================
 * COMPILE-TIME VALIDATION
 * ============================================================================ */

/* Ensure heap is within identity-mapped region */
_Static_assert(KERNEL_HEAP_END <= IDENTITY_MAP_END, 
               "ERROR: Kernel heap exceeds identity-mapped region");

/* Ensure we don't exceed page table limit */
_Static_assert(NUM_KERNEL_PAGE_TABLES <= MAX_PAGE_TABLES,
               "ERROR: Too many page tables required - reduce KERNEL_HEAP_SIZE_MB");

/* Ensure user space starts after kernel heap */
_Static_assert(USER_SPACE_START == KERNEL_HEAP_END,
               "ERROR: User space must start immediately after kernel heap");

/* Ensure kernel space matches heap */
_Static_assert(KERNEL_SPACE_END == KERNEL_HEAP_END,
               "ERROR: Kernel space end must match heap end");

/* Ensure heap size is reasonable */
_Static_assert(KERNEL_HEAP_SIZE_MB >= 4 && KERNEL_HEAP_SIZE_MB <= 32,
               "ERROR: KERNEL_HEAP_SIZE_MB must be between 4 and 32");

/* ============================================================================
 * LEGACY COMPATIBILITY (for gradual migration)
 * ============================================================================ */

/* Old constant names - map to new centralized values */
#define KERNEL_BASE             0xC0000000
#define KERNEL_STACK_SIZE       0x2000      /* 8KB */
#define USER_STACK_SIZE         0x100000    /* 1MB */
#define HEAP_START              USER_CODE_BASE

/* Heap configuration for heap.c */
#define HEAP_MIN_SIZE           (KERNEL_HEAP_SIZE_MB * MB)
#define HEAP_MAX_SIZE           0x10000000  /* 256MB theoretical max */
#define HEAP_ALIGNMENT          0x1000      /* 4KB page alignment */

/* ============================================================================
 * DOCUMENTATION
 * ============================================================================ */

/*
 * HOW TO CHANGE HEAP SIZE:
 * 
 * 1. Edit KERNEL_HEAP_SIZE_MB above (e.g., 8 → 16)
 * 2. Recompile: make clean && make iso
 * 3. Boot and verify
 * 
 * All other constants update automatically:
 * - KERNEL_HEAP_END
 * - USER_SPACE_START
 * - IDENTITY_MAP_END
 * - NUM_KERNEL_PAGE_TABLES
 * 
 * Compile-time assertions will catch any errors.
 */

/*
 * MEMORY LAYOUT EXAMPLE (KERNEL_HEAP_SIZE_MB = 8):
 * 
 * 0x00000000 - 0x00100000 (0-1MB)     : BIOS/VGA/Boot
 * 0x00100000 - 0x00200000 (1-2MB)     : Kernel code/data
 * 0x00200000 - 0x00A00000 (2-10MB)    : Kernel heap (8MB)
 * 0x00A00000 - 0x80000000 (10MB-2GB)  : User space
 * 0x80000000 - 0xC0000000 (2GB-3GB)   : User stack
 * 0xC0000000 - 0xFFFFFFFF (3GB-4GB)   : Kernel virtual
 * 
 * Identity mapping: 0x00000000 - 0x00C00000 (0-12MB, 3 page tables)
 */

#endif /* KERNEL_MEMORY_LAYOUT_H */
