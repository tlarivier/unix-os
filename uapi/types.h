#ifndef UAPI_TYPES_H
#define UAPI_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int32_t  __kernel_pid_t;
typedef uint32_t __kernel_uid_t;
typedef uint32_t __kernel_gid_t;
typedef uint32_t __kernel_dev_t;
typedef uint32_t __kernel_ino_t;
typedef uint32_t __kernel_mode_t;
typedef int32_t  __kernel_off_t;
typedef int32_t  __kernel_time_t;
typedef int      __kernel_fd_t;

typedef size_t   __kernel_size_t;
typedef int32_t  __kernel_ssize_t;

typedef uint32_t __kernel_syscall_t;

typedef int32_t  __kernel_errno_t;

typedef uint32_t __kernel_sigset_t;

struct __kernel_procinfo {
    __kernel_pid_t  pid;
    __kernel_pid_t  ppid;
    __kernel_uid_t  uid;
    __kernel_gid_t  gid;
    uint32_t        state;
    char            name[32];
    uint32_t        memory_kb;
    uint32_t        cpu_time_ms;
} __attribute__((packed));

struct __kernel_stat {
    __kernel_dev_t   st_dev;
    __kernel_ino_t   st_ino;  
    __kernel_mode_t  st_mode;
    uint32_t         st_nlink;
    __kernel_uid_t   st_uid;
    __kernel_gid_t   st_gid;
    __kernel_dev_t   st_rdev;
    __kernel_off_t   st_size;
    uint32_t         st_blksize;
    uint32_t         st_blocks;
    __kernel_time_t  st_atime;
    __kernel_time_t  st_mtime;
    __kernel_time_t  st_ctime;
} __attribute__((packed));

#endif 
