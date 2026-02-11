#ifndef _LIBC_SYSCALL_H
#define _LIBC_SYSCALL_H

#include <stdint.h>

#define SYSCALL_X(name, nr, nargs) static const int SYS_##name = nr;
#include "../../uapi/syscalls.def"
#undef SYSCALL_X

#define  __NR_exit      1
#define  __NR_fork      2
#define  __NR_waitpid   3
#define  __NR_getpid    4
#define  __NR_getppid   5
#define  __NR_execve    7
#define  __NR_read      10
#define  __NR_write     11
#define  __NR_open      12
#define  __NR_close     13
#define  __NR_lseek     14
#define  __NR_dup       15
#define  __NR_dup2      16
#define  __NR_pipe      17
#define  __NR_stat      20
#define  __NR_fstat     21
#define  __NR_mkdir     22
#define  __NR_rmdir     23
#define  __NR_unlink    24
#define  __NR_getdents  30
#define  __NR_chdir     31
#define  __NR_getcwd    32
#define  __NR_brk       40
#define  __NR_mmap      41
#define  __NR_munmap    42

#include "syscall_inline.h"

#endif 
