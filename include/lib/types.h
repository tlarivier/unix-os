#ifndef LIB_TYPES_H
#define LIB_TYPES_H

/*
 * System-wide Type Definitions
 * 
 * "Define it once, define it right, use it everywhere" - Linus Philosophy
 * 
 * This header provides the basic types for the entire system:
 * - Kernel code includes this
 * - Userspace code includes this  
 * - Library code includes this
 * 
 * NO OTHER FILE should redefine these types. Period.
 */

/* Compiler-provided basic types */
#include <stdint.h>
#include <stddef.h>

/*
 * POSIX-compliant types - same as Linux
 */

/* ssize_t - signed size type for syscalls that return size OR negative error */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef __x86_64__
typedef long ssize_t;           /* 64-bit: use long (8 bytes) */
#else
typedef int32_t ssize_t;        /* 32-bit: use int32_t (4 bytes) */
#endif
#endif

/* File system types */
#ifndef _FILE_TYPES_DEFINED
#define _FILE_TYPES_DEFINED
typedef int32_t  off_t;         /* File offset */
typedef uint32_t mode_t;        /* File mode/permissions */
typedef uint32_t uid_t;         /* User ID */
typedef uint32_t gid_t;         /* Group ID */
typedef uint32_t dev_t;         /* Device ID */
typedef uint32_t ino_t;         /* Inode number */
typedef uint32_t blksize_t;     /* Block size */
typedef uint32_t blkcnt_t;      /* Block count */
#endif

/* Process types */
#ifndef _PROCESS_TYPES_DEFINED  
#define _PROCESS_TYPES_DEFINED
typedef int32_t pid_t;          /* Process ID */
typedef int32_t time_t;         /* Time value */
#endif

/* System types */
#ifndef _SYSTEM_TYPES_DEFINED
#define _SYSTEM_TYPES_DEFINED
typedef uint32_t useconds_t;    /* Microseconds */
#ifndef _SUSECONDS_T_DEFINED
#define _SUSECONDS_T_DEFINED
typedef int32_t suseconds_t;   /* Signed microseconds */
#endif
#endif

/*
 * Kernel-specific types (only when building kernel)
 */
#ifdef KERNEL_MODE
typedef uint32_t paddr_t;       /* Physical address */
typedef uint32_t vaddr_t;       /* Virtual address */  
typedef uint32_t pfn_t;         /* Page frame number */
#endif

/*
 * Convenience macros - Linus style
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); })

/*
 * Compiler attributes
 */
#define __packed        __attribute__((packed))
#define __aligned(x)    __attribute__((aligned(x)))
#define __noreturn      __attribute__((noreturn))
#define __unused        __attribute__((unused))
#define __maybe_unused  __attribute__((maybe_unused))

#endif /* LIB_TYPES_H */
