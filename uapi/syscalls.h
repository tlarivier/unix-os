/*
 * syscalls.h - System Call Interface (X-macro based)
 * 
 * Generated from syscalls.def - DO NOT EDIT MANUALLY
 */

#ifndef UAPI_SYSCALLS_H
#define UAPI_SYSCALLS_H

#include "types.h"

/*
 * Generate syscall number enum
 */
#define SYSCALL_X(name, nr, nargs) __NR_##name = nr,
enum {
#include "syscalls.def"
    __NR_MAX = 255
};
#undef SYSCALL_X

/*
 * Syscall argument limits
 */
#define SYSCALL_MAX_ARGS 6

/*
 * File operation flags - POSIX O_* flags
 */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001  
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_EXCL      0x0200
#define O_TRUNC     0x0400
#define O_APPEND    0x0800
#define O_NONBLOCK  0x1000
#ifndef O_CLOEXEC
#define O_CLOEXEC   0x80000
#endif

/*
 * File type and mode bits
 */
#define S_IFMT      0170000
#define S_IFSOCK    0140000
#define S_IFLNK     0120000
#define S_IFREG     0100000
#define S_IFBLK     0060000
#define S_IFDIR     0040000
#define S_IFCHR     0020000
#define S_IFIFO     0010000

#define S_ISUID     0004000
#define S_ISGID     0002000
#define S_ISVTX     0001000

#define S_IRWXU     0000700
#define S_IRUSR     0000400
#define S_IWUSR     0000200
#define S_IXUSR     0000100
#define S_IRWXG     0000070
#define S_IRGRP     0000040
#define S_IWGRP     0000020
#define S_IXGRP     0000010
#define S_IRWXO     0000007
#define S_IROTH     0000004
#define S_IWOTH     0000002
#define S_IXOTH     0000001

/* File type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/*
 * Wait options
 */
#define WNOHANG     0x00000001
#define WUNTRACED   0x00000002
#define WCONTINUED  0x00000008

/* Wait status macros */
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFEXITED(s)    (WTERMSIG(s) == 0)
#define WIFSIGNALED(s)  (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)

/*
 * Seek whence
 */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/*
 * Directory entry for getdents()
 * Note: struct linux_dirent is defined in kernel/vfs.h for kernel
 * and here for userspace only
 */
#if !defined(KERNEL_MODE) && !defined(_LINUX_DIRENT_DEFINED)
#define _LINUX_DIRENT_DEFINED
struct linux_dirent {
    __kernel_ino_t  d_ino;
    __kernel_off_t  d_off;
    uint16_t        d_reclen;
    uint8_t         d_type;
    char            d_name[];
} __attribute__((packed));
#endif

/* Directory entry types */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12

/*
 * Get syscall name string (for debugging)
 */
const char *syscall_name(int nr);

#endif /* UAPI_SYSCALLS_H */
