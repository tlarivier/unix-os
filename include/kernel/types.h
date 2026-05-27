#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <../uapi/types.h>

typedef __kernel_pid_t pid_t;
typedef __kernel_uid_t uid_t;
typedef __kernel_gid_t gid_t;
typedef __kernel_dev_t dev_t;
typedef __kernel_ino_t ino_t;
typedef __kernel_mode_t mode_t;
typedef __kernel_off_t off_t;
typedef __kernel_time_t time_t;
typedef __kernel_ssize_t ssize_t;

typedef int32_t tid_t;
typedef uint32_t nlink_t;
typedef uint32_t blksize_t;
typedef uint32_t blkcnt_t;
typedef int32_t suseconds_t;

typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#endif
