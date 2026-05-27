#ifndef LIB_TYPES_H
#define LIB_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#ifdef __x86_64__
typedef long ssize_t;
#else
typedef int32_t ssize_t;
#endif
#endif

#ifndef _FILE_TYPES_DEFINED
#define _FILE_TYPES_DEFINED
typedef int32_t off_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t dev_t;
typedef uint32_t ino_t;
typedef uint32_t blksize_t;
typedef uint32_t blkcnt_t;
#endif

#ifndef _PROCESS_TYPES_DEFINED
#define _PROCESS_TYPES_DEFINED
typedef int32_t pid_t;
typedef int32_t time_t;
#endif

#ifndef _SYSTEM_TYPES_DEFINED
#define _SYSTEM_TYPES_DEFINED
typedef uint32_t useconds_t;
#ifndef _SUSECONDS_T_DEFINED
#define _SUSECONDS_T_DEFINED
typedef int32_t suseconds_t;
#endif
#endif

#ifdef KERNEL_MODE
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;
typedef uint32_t pfn_t;
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __noreturn __attribute__((noreturn))
#define __unused __attribute__((unused))
#define __maybe_unused __attribute__((maybe_unused))

#endif /* LIB_TYPES_H */
