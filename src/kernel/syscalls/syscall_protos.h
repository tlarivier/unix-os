#ifndef KERNEL_SYSCALLS_PROTOS_H
#define KERNEL_SYSCALLS_PROTOS_H

#include <stdint.h>

int32_t sys_open(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_close(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_read(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_write(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_lseek(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_stat(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_fstat(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_dup(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_dup2(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_mkdir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_chdir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getcwd(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_unlink(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getdents(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_chmod(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_chown(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_rename(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_rmdir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_truncate(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_ftruncate(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_brk(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_mmap(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_munmap(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_mprotect(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getppid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_geteuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getegid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_setuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_setgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_seteuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_setegid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_waitpid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_kill(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_signal(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_gettid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_set_tid_address(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_fork_wrap(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_gettimeofday(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_nanosleep(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_clock_gettime(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_time(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_gfx_mode(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_gfx_palette(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_kb_event(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_reboot(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_setpgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getpgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_setsid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_getsid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_tcgetpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_tcsetpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

int32_t sys_tcgetattr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_tcsetattr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_isatty(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sys_ttyname(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

struct rlimit;
int sys_execve(const char *pathname, char *const argv[], char *const envp[]);
int sys_pipe(int pipefd[2]);
int sys_getrlimit(int resource, struct rlimit *rlim);
int sys_setrlimit(int resource, const struct rlimit *rlim);
int sys_nice(int increment);
int32_t sys_fork(void *regs_ptr);

#endif /* KERNEL_SYSCALLS_PROTOS_H */
