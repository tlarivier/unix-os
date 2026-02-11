#ifndef KERNEL_CONSTANTS_H
#define KERNEL_CONSTANTS_H

#include <stdint.h>
#include <stddef.h>

enum gdt_selectors {
    NULL_SEL         =  0x00,
    KERNEL_CODE_SEL  =  0x08,
    KERNEL_DATA_SEL  =  0x10,
    USER_CODE_SEL    =  0x1B,
    USER_DATA_SEL    =  0x23,
    TSS_SEL          =  0x2B
};

enum page_flags {
    PAGE_PRESENT        =  0x001,
    PAGE_WRITABLE       =  0x002,
    PAGE_USER           =  0x004,
    PAGE_WRITE_THROUGH  =  0x008,
    PAGE_CACHE_DISABLE  =  0x010,
    PAGE_ACCESSED       =  0x020,
    PAGE_DIRTY          =  0x040,
    PAGE_SIZE_FLAG      =  0x080,
    PAGE_GLOBAL         =  0x100
};

enum system_constants {
    PAGE_SIZE_CONST         =  4096,
    MAX_PROCESSES_CONST     =  128,
    PROCESS_NAME_MAX_CONST  =  64,
    MAX_OPEN_FILES_CONST    =  64,
    PID_MAX_CONST           =  32768,
    MAX_SYSCALLS_CONST      =  256
};

#include <kernel/memory_layout.h>


enum stack_protection {
    STACK_CANARY_VALUE          =  0xDEADBEEF,
    STACK_CANARY_PROCESS_MAGIC  =  0xC0DEC0DE
};

enum memory_protection_magic {
    GUARD_PAGE_MAGIC         =  0xCAFEBABE,
    FREED_MEMORY_PATTERN     =  0xDEADDEAD,
    UNINITIALIZED_PATTERN    =  0xCCCCCCCC,
    ALLOCATION_HEADER_MAGIC  =  0xABCDDCBA
};

enum slub_constants {
    SLUB_CACHE_MAGIC   =  0x534C5542,
    SLUB_OBJECT_MAGIC  =  0x4F424A54,
    SLUB_FREE_MAGIC    =  0x46524545
};

enum vga_framebuffer {
    VGA_FRAMEBUFFER_ADDR = 0xA0000,
    VGA_FRAMEBUFFER_SIZE = 0x10000
};

enum ipc_constants {
    PIPE_FD_BASE    =  1000,
    PIPE_MAX_PIPES  =  64,
    PIPE_BUF_SIZE   =  4096
};

enum fs_constants {
    PATH_MAX_CONST = 256,
    NAME_MAX_CONST = 255,
    BUF_SIZE_CONST = 256
};

#endif
