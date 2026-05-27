#ifndef UAPI_TYPES_H
#define UAPI_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t __kernel_pid_t;
typedef uint32_t __kernel_uid_t;
typedef uint32_t __kernel_gid_t;
typedef uint32_t __kernel_dev_t;
typedef uint32_t __kernel_ino_t;
typedef uint32_t __kernel_mode_t;
typedef int32_t __kernel_off_t;
typedef int32_t __kernel_time_t;
typedef int __kernel_fd_t;

typedef size_t __kernel_size_t;
typedef int32_t __kernel_ssize_t;

typedef uint32_t __kernel_syscall_t;

typedef int32_t __kernel_errno_t;

typedef uint32_t __kernel_sigset_t;

struct __kernel_procinfo {
  __kernel_pid_t pid;
  __kernel_pid_t ppid;
  __kernel_uid_t uid;
  __kernel_gid_t gid;
  uint32_t state;
  char name[32];
  uint32_t memory_kb;
  uint32_t cpu_time_ms;
} __attribute__((packed));

/* POSIX `struct stat` — the canonical UAPI definition for both kernel
 * internals and userspace. sys/stat.h re-exports this. */
struct stat {
  uint32_t st_dev;        /*  0 */
  uint32_t st_ino;        /*  4 */
  uint16_t st_mode;       /*  8 */
  uint16_t st_nlink;      /* 10 */
  uint16_t st_uid;        /* 12 */
  uint16_t st_gid;        /* 14 */
  uint32_t st_rdev;       /* 16 */
  int32_t st_size;        /* 20 */
  uint32_t st_blksize;    /* 24 */
  uint32_t st_blocks;     /* 28 */
  uint32_t st_atime;      /* 32 */
  uint32_t st_atime_nsec; /* 36 */
  uint32_t st_mtime;      /* 40 */
  uint32_t st_mtime_nsec; /* 44 */
  uint32_t st_ctime;      /* 48 */
  uint32_t st_ctime_nsec; /* 52 */
  uint32_t __unused4;     /* 56 */
  uint32_t __unused5;     /* 60 */
};

#endif
