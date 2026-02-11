#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#include <stdint.h>
#include <stddef.h>
#include <../uapi/syscalls.h>
#include <kernel/errno.h>

typedef int32_t (*syscall_handler_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);

void syscall_init(void);
int register_syscall(int number, syscall_handler_t handler);

int32_t syscall_dispatch(int number, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);

int32_t sys_read(uint32_t fd, uint32_t buffer, uint32_t count, uint32_t arg4, uint32_t arg5);
int32_t sys_write(uint32_t fd, uint32_t buffer, uint32_t count, uint32_t arg4, uint32_t arg5);
int32_t sys_open(uint32_t path, uint32_t flags, uint32_t mode, uint32_t unused, uint32_t arg5);
int32_t sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_fork(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);
int32_t sys_exec(uint32_t path, uint32_t argv, uint32_t envp, uint32_t unused, uint32_t arg5);
int32_t sys_wait(uint32_t pid, uint32_t status, uint32_t options, uint32_t unused, uint32_t arg5);
int32_t sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_kill(uint32_t pid, uint32_t signal, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);

int32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, uint32_t arg5);
int32_t sys_munmap(uint32_t addr, uint32_t length, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_brk(uint32_t addr, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);

int32_t sys_mkdir(uint32_t path, uint32_t mode, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_rmdir(uint32_t path, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_chmod(uint32_t path, uint32_t mode, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_chown(uint32_t path, uint32_t owner, uint32_t group, uint32_t unused, uint32_t arg5);
int32_t sys_link(uint32_t oldpath, uint32_t newpath, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_unlink(uint32_t path, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_rename(uint32_t oldpath, uint32_t newpath, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_stat(uint32_t path, uint32_t statbuf, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_fstat(uint32_t fd, uint32_t statbuf, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t unused, uint32_t arg5);

int32_t sys_opendir(uint32_t path, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_readdir(uint32_t dirfd, uint32_t dirent, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_closedir(uint32_t dirfd, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);

int32_t sys_signal(uint32_t signum, uint32_t handler, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_sigaction(uint32_t signum, uint32_t act, uint32_t oldact, uint32_t unused, uint32_t arg5);
int32_t sys_sigprocmask(uint32_t how, uint32_t set, uint32_t oldset, uint32_t unused, uint32_t arg5);
int32_t sys_sigsuspend(uint32_t mask, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_sigreturn(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);

int32_t sys_time(uint32_t t, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_times(uint32_t buf, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_sleep(uint32_t seconds, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_nanosleep(uint32_t req, uint32_t rem, uint32_t unused1, uint32_t unused2, uint32_t arg5);

int32_t sys_uname(uint32_t buf, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_getuid(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);
int32_t sys_getgid(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);
int32_t sys_setuid(uint32_t uid, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_setgid(uint32_t gid, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);

#endif 
