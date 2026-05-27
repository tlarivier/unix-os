/*
 * syscall_table.c — sole INT 0x80 dispatcher: the static handler table
 * syscall_handlers[__NR_MAX+1] and the syscall_handler(syscall_registers_t*)
 * called from syscall_entry.S, plus five ABI shims for syscalls whose
 * native signature isn't (uint32_t x5).
 *
 * Invariants:
 *  - syscall_handlers[] is static; every non-NULL slot has signature
 *    int32_t(*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t).
 *  - nr >= __NR_MAX+1 or NULL slot returns -ENOSYS.
 *  - process_check_current_canary() runs at prologue AND epilogue.
 *  - Only this TU reads/writes the user syscall frame's eax field.
 *
 * Not allowed:
 *  - Casting a differently-typed function pointer into the table.
 *  - Implementing any syscall semantics here — delegate to sys_<X>.
 *  - Registering syscalls dynamically at runtime (table is build-time data).
 */

#include "syscall.h"
#include "syscall_protos.h"
#include <kernel/errno.h>
#include <kernel/exec.h>
#include <kernel/limits.h>
#include <kernel/percpu.h>
#include <kernel/priority.h>
#include <kernel/process.h>
#include <kernel/syscall_handler.h>
#include <uapi/syscalls.h>

static int32_t shim_execve(uint32_t pathname, uint32_t argv, uint32_t envp,
                           uint32_t u4, uint32_t u5) {
  (void)u4;
  (void)u5;
  return (int32_t)sys_execve((const char *)pathname, (char *const *)argv,
                             (char *const *)envp);
}

static int32_t shim_pipe(uint32_t pipefd_ptr, uint32_t u2, uint32_t u3,
                         uint32_t u4, uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)sys_pipe((int *)pipefd_ptr);
}

static int32_t shim_getrlimit(uint32_t resource, uint32_t rlim_ptr, uint32_t u3,
                              uint32_t u4, uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)sys_getrlimit((int)resource, (struct rlimit *)rlim_ptr);
}

static int32_t shim_setrlimit(uint32_t resource, uint32_t rlim_ptr, uint32_t u3,
                              uint32_t u4, uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)sys_setrlimit((int)resource, (const struct rlimit *)rlim_ptr);
}

static int32_t shim_nice(uint32_t increment, uint32_t u2, uint32_t u3,
                         uint32_t u4, uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)sys_nice((int)(int32_t)increment);
}

#define SYSCALL_TABLE_SIZE (__NR_MAX + 1)

static syscall_fn_t syscall_handlers[SYSCALL_TABLE_SIZE] = {
    [__NR_exit] = sys_exit,
    [__NR_fork] = sys_fork_wrap,
    [__NR_waitpid] = sys_waitpid,
    [__NR_getpid] = sys_getpid,
    [__NR_getppid] = sys_getppid,
    [__NR_execve] = shim_execve,
    [__NR_read] = sys_read,
    [__NR_write] = sys_write,
    [__NR_open] = sys_open,
    [__NR_close] = sys_close,
    [__NR_lseek] = sys_lseek,
    [__NR_dup] = sys_dup,
    [__NR_dup2] = sys_dup2,
    [__NR_pipe] = shim_pipe,
    [__NR_stat] = sys_stat,
    [__NR_fstat] = sys_fstat,
    [__NR_mkdir] = sys_mkdir,
    [__NR_unlink] = sys_unlink,
    [__NR_getdents] = sys_getdents,
    [__NR_chdir] = sys_chdir,
    [__NR_getcwd] = sys_getcwd,
    [__NR_brk] = sys_brk,
    [__NR_mmap] = sys_mmap,
    [__NR_munmap] = sys_munmap,
    [__NR_signal] = sys_signal,
    [__NR_kill] = sys_kill,
    [__NR_time] = sys_time,
    [__NR_nanosleep] = sys_nanosleep,
    [__NR_gettimeofday] = sys_gettimeofday,
    [__NR_clock_gettime] = sys_clock_gettime,
    [__NR_mprotect] = sys_mprotect,
    [__NR_getuid] = sys_getuid,
    [__NR_getgid] = sys_getgid,
    [__NR_geteuid] = sys_geteuid,
    [__NR_getegid] = sys_getegid,
    [__NR_setuid] = sys_setuid,
    [__NR_getrlimit] = shim_getrlimit,
    [__NR_setrlimit] = shim_setrlimit,
    [__NR_nice] = shim_nice,
    [__NR_reboot] = sys_reboot,
    [__NR_setgid] = sys_setgid,
    [__NR_seteuid] = sys_seteuid,
    [__NR_setpgid] = sys_setpgid,
    [__NR_getpgid] = sys_getpgid,
    [__NR_getpgrp] = sys_getpgrp,
    [__NR_setsid] = sys_setsid,
    [__NR_getsid] = sys_getsid,
    [__NR_tcgetpgrp] = sys_tcgetpgrp,
    [__NR_tcsetpgrp] = sys_tcsetpgrp,
    [__NR_tcgetattr] = sys_tcgetattr,
    [__NR_tcsetattr] = sys_tcsetattr,
    [__NR_isatty] = sys_isatty,
    [__NR_ttyname] = sys_ttyname,
    [__NR_chmod] = sys_chmod,
    [__NR_chown] = sys_chown,
    [__NR_rename] = sys_rename,
    [__NR_rmdir] = sys_rmdir,
    [__NR_truncate] = sys_truncate,
    [__NR_ftruncate] = sys_ftruncate,
    [__NR_setegid] = sys_setegid,
    [__NR_set_tid_address] = sys_set_tid_address,
    [__NR_gettid] = sys_gettid,
    [__NR_gfx_mode] = sys_gfx_mode,
    [__NR_gfx_palette] = sys_gfx_palette,
    [__NR_kb_event] = sys_kb_event,
};

void syscall_handler(syscall_registers_t *regs) {
  uint32_t nr = regs->eax;

  process_check_current_canary();

  if (nr >= SYSCALL_TABLE_SIZE || !syscall_handlers[nr]) {
    regs->eax = -ENOSYS;
    return;
  }

  this_cpu()->syscall_regs = regs;

  regs->eax = syscall_handlers[nr](regs->ebx, regs->ecx, regs->edx, regs->esi,
                                   regs->edi);

  process_check_current_canary();
}
