#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>
#include <kernel/process.h>
#include <kernel/uaccess.h>
#include <kernel/errno.h>
#include <kernel/vfs.h>
#include <../uapi/syscalls.h>

typedef int32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct {
    syscall_fn_t handler;
    const char*  name;
} syscall_entry_t;

extern syscall_entry_t syscall_table[];

struct timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414

#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

int32_t sys_open(uint32_t path, uint32_t flags, uint32_t mode, uint32_t, uint32_t);
int32_t sys_close(uint32_t fd, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t, uint32_t);
int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t, uint32_t);
int32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t, uint32_t);
int32_t sys_stat(uint32_t path, uint32_t buf, uint32_t, uint32_t, uint32_t);
int32_t sys_fstat(uint32_t fd, uint32_t buf, uint32_t, uint32_t, uint32_t);
int32_t sys_dup(uint32_t fd, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t, uint32_t, uint32_t);
int32_t sys_mkdir(uint32_t path, uint32_t mode, uint32_t, uint32_t, uint32_t);
int32_t sys_chdir(uint32_t path, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getcwd(uint32_t buf, uint32_t size, uint32_t, uint32_t, uint32_t);
int32_t sys_unlink(uint32_t path, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_access(uint32_t path, uint32_t mode, uint32_t, uint32_t, uint32_t);
int32_t sys_getdents(uint32_t fd, uint32_t buf, uint32_t size, uint32_t, uint32_t);

int32_t sys_brk(uint32_t addr, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_mmap(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags, uint32_t fd);
int32_t sys_munmap(uint32_t addr, uint32_t len, uint32_t, uint32_t, uint32_t);
int32_t sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot, uint32_t, uint32_t);

int32_t sys_exit(uint32_t status, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_waitpid(uint32_t pid, uint32_t status, uint32_t opts, uint32_t, uint32_t);
int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t, uint32_t, uint32_t);
int32_t sys_signal(uint32_t sig, uint32_t handler, uint32_t, uint32_t, uint32_t);

int32_t sys_gettimeofday(uint32_t tv, uint32_t tz, uint32_t, uint32_t, uint32_t);
int32_t sys_nanosleep(uint32_t req, uint32_t rem, uint32_t, uint32_t, uint32_t);
int32_t sys_clock_gettime(uint32_t clk_id, uint32_t tp, uint32_t, uint32_t, uint32_t);
int32_t sys_time(uint32_t tloc, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_uname(uint32_t buf, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_ioctl(uint32_t fd, uint32_t req, uint32_t arg, uint32_t, uint32_t);
int32_t sys_fcntl(uint32_t fd, uint32_t cmd, uint32_t arg, uint32_t, uint32_t);
int32_t sys_reboot(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_select(uint32_t nfds, uint32_t readfds, uint32_t writefds, uint32_t exceptfds, uint32_t timeout);
int32_t sys_poll(uint32_t fds, uint32_t nfds, uint32_t timeout_ms, uint32_t, uint32_t);
int32_t sys_getenv(uint32_t name, uint32_t buf, uint32_t size, uint32_t, uint32_t);

#endif 
