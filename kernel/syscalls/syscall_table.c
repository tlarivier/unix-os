#include "syscall.h"
#include <kernel/syscall_handler.h>
#include <kernel/ipc.h>
#include <kernel/limits.h>
#include <kernel/priority.h>
#include <kernel/exec.h>
#include <kernel/process.h>
#include <kernel/kprintf.h>
#include <kernel/uaccess.h>

extern int32_t sys_sigaction(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_sigprocmask(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_sigsuspend(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_sigpending(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_setpgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_getpgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_getpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_setsid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_getsid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_tcgetpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_tcsetpgrp(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_tcgetattr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_tcsetattr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_isatty(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_ttyname(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_geteuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_getegid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_setuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_setgid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_seteuid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_setegid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_chmod(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_fchmod(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_chown(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_fchown(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_rename(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_rmdir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_link(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_symlink(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_readlink(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_umask(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_truncate(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_ftruncate(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_flock(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_shmget(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_shmat(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_shmdt(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_shmctl(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_semget(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_semop(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_semctl(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_clone(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_futex(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_set_tid_address(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_gettid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_socket(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_bind(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_listen(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_accept(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_connect(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_send(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_recv(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_shutdown(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_abi_version(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int32_t sys_alarm(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* Dynamic linking removed from kernel - belongs in userspace ld.so */

static int32_t sys_stub(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    return -ENOSYS;
}

static int32_t sys_pipe_wrap(uint32_t fds, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    return sys_pipe((int*)fds);
}

static int32_t sys_pipe2_wrap(uint32_t fds, uint32_t flags, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    return sys_pipe2((int*)fds, (int)flags);
}

static int32_t sys_execve_wrap(uint32_t path, uint32_t argv, uint32_t envp, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    return sys_execve((const char*)path, (char* const*)argv, (char* const*)envp);
}

static int32_t sys_nice_wrap(uint32_t inc, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    return sys_nice((int)inc);
}

static int32_t sys_getrlimit_wrap(uint32_t res, uint32_t rlim, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    return sys_getrlimit((int)res, (struct rlimit*)rlim);
}

static int32_t sys_setrlimit_wrap(uint32_t res, uint32_t rlim, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    return sys_setrlimit((int)res, (const struct rlimit*)rlim);
}

extern int32_t sys_fork(syscall_registers_t* regs);
static syscall_registers_t* current_syscall_regs = NULL;
static int32_t sys_fork_wrap(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    return sys_fork(current_syscall_regs);
}

static int32_t sys_getppid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* p = get_current_process();
    return p ? p->ppid : 0;
}

#define SYSCALL_MAX 256
static syscall_fn_t syscall_handlers[SYSCALL_MAX] = {
    [0]                 = sys_stub,
    [__NR_exit]         = sys_exit,
    [__NR_fork]         = sys_fork_wrap,
    [__NR_waitpid]      = sys_waitpid,
    [__NR_getpid]       = sys_getpid,
    [__NR_getppid]      = sys_getppid,
    [__NR_execve]       = sys_execve_wrap,
    [__NR_read]         = sys_read,
    [__NR_write]        = sys_write,
    [__NR_open]         = sys_open,
    [__NR_close]        = sys_close,
    [__NR_lseek]        = sys_lseek,
    [__NR_dup]          = sys_dup,
    [__NR_dup2]         = sys_dup2,
    [__NR_pipe]         = sys_pipe_wrap,
    [__NR_pipe2]        = sys_pipe2_wrap,
    [__NR_stat]         = sys_stat,
    [__NR_fstat]        = sys_fstat,
    [__NR_mkdir]        = sys_mkdir,
    [__NR_unlink]       = sys_unlink,
    [__NR_getdents]     = sys_getdents,
    [__NR_chdir]        = sys_chdir,
    [__NR_getcwd]       = sys_getcwd,
    [__NR_brk]          = sys_brk,
    [__NR_mmap]         = sys_mmap,
    [__NR_munmap]       = sys_munmap,
    [__NR_signal]       = sys_signal,
    [__NR_kill]         = sys_kill,
    [__NR_sigaction]    = sys_sigaction,
    [__NR_sigprocmask]  = sys_sigprocmask,
    [__NR_sigsuspend]   = sys_sigsuspend,
    [__NR_sigpending]   = sys_sigpending,
    [__NR_ioctl]        = sys_ioctl,
    [__NR_fcntl]        = sys_fcntl,
    [__NR_time]         = sys_time,
    [__NR_nanosleep]    = sys_nanosleep,
    [__NR_gettimeofday] = sys_gettimeofday,
    [__NR_clock_gettime] = sys_clock_gettime,
    [__NR_alarm]        = sys_alarm,
    [__NR_mprotect]     = sys_mprotect,
    [__NR_uname]        = sys_uname,
    [__NR_getuid]       = sys_getuid,
    [__NR_getgid]       = sys_getgid,
    [__NR_geteuid]      = sys_geteuid,
    [__NR_getegid]      = sys_getegid,
    [__NR_setuid]       = sys_setuid,
    [__NR_getrlimit]    = sys_getrlimit_wrap,
    [__NR_setrlimit]    = sys_setrlimit_wrap,
    [__NR_nice]         = sys_nice_wrap,
    [__NR_reboot]       = sys_reboot,
    [__NR_umask]        = sys_umask,
    [__NR_setgid]       = sys_setgid,
    [__NR_seteuid]      = sys_seteuid,
    [__NR_select]       = sys_select,
    [__NR_poll]         = sys_poll,
    [__NR_setpgid]      = sys_setpgid,
    [__NR_getpgid]      = sys_getpgid,
    [__NR_getpgrp]      = sys_getpgrp,
    [__NR_setsid]       = sys_setsid,
    [__NR_getsid]       = sys_getsid,
    [__NR_tcgetpgrp]    = sys_tcgetpgrp,
    [__NR_tcsetpgrp]    = sys_tcsetpgrp,
    [__NR_tcgetattr]    = sys_tcgetattr,
    [__NR_tcsetattr]    = sys_tcsetattr,
    [__NR_isatty]       = sys_isatty,
    [__NR_ttyname]      = sys_ttyname,
    [__NR_chmod]        = sys_chmod,
    [__NR_chown]        = sys_chown,
    [__NR_rename]       = sys_rename,
    [__NR_rmdir]        = sys_rmdir,
    [__NR_fchmod]       = sys_fchmod,
    [__NR_fchown]       = sys_fchown,
    [__NR_link]         = sys_link,
    [__NR_symlink]      = sys_symlink,
    [__NR_readlink]     = sys_readlink,
    [__NR_truncate]     = sys_truncate,
    [__NR_ftruncate]    = sys_ftruncate,
    [__NR_flock]        = sys_flock,
    [__NR_setegid]      = sys_setegid,
    [__NR_shmget]       = sys_shmget,
    [__NR_shmat]        = sys_shmat,
    [__NR_shmdt]        = sys_shmdt,
    [__NR_shmctl]       = sys_shmctl,
    [__NR_semget]       = sys_semget,
    [__NR_semop]        = sys_semop,
    [__NR_semctl]       = sys_semctl,
    [__NR_clone]        = sys_clone,
    [__NR_futex]        = sys_futex,
    [__NR_set_tid_address] = sys_set_tid_address,
    [__NR_gettid]       = sys_gettid,
    [__NR_socket]       = sys_socket,
    [__NR_bind]         = sys_bind,
    [__NR_listen]       = sys_listen,
    [__NR_accept]       = sys_accept,
    [__NR_connect]      = sys_connect,
    [__NR_send]         = sys_send,
    [__NR_recv]         = sys_recv,
    [__NR_shutdown]     = sys_shutdown,
    [__NR_abi_version]  = sys_abi_version,
};

void syscall_handler(syscall_registers_t* regs) {
    uint32_t nr = regs->eax;
    
    process_t* cur = get_current_process();
    if (cur && cur->pid >= 3 && nr != 11) {
    }
    
    if (nr >= SYSCALL_MAX || !syscall_handlers[nr]) {
        regs->eax = -ENOSYS;
        return;
    }
    
    current_syscall_regs = regs;
    
    regs->eax = syscall_handlers[nr](
        regs->ebx,
        regs->ecx,
        regs->edx,
        regs->esi,
        regs->edi
    );
}

void syscall_init(void) {
    /* Static table already initialized - nothing to do */
}
