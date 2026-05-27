/*
 * sys_proc.c — marshalling for ~25 process/identity/signal/job-control
 * syscalls (exit, getpid/ppid, get/set uid+gid+euid+egid, waitpid, kill,
 * signal, gettid, set_tid_address, fork_wrap, setpgid/getpgid/getpgrp/
 * setsid/getsid/tcgetpgrp/tcsetpgrp), delegating to cred/process/jobctl.
 *
 * Invariants:
 *  - Uniform (uint32_t x5) -> int32_t ABI on every wrapper.
 *  - Credential edits go through cred_set_{uid,gid,euid,egid}.
 *  - sys_fork_wrap reads this_cpu()->syscall_regs to reconstruct the frame.
 *  - Signal handler pointers are validated against [USER_CODE_BASE,
 * USER_SPACE_END).
 *
 * Not allowed:
 *  - Writing cur->uid/euid/suid/gid/egid/sgid by hand (use cred_*).
 *  - Implementing waitpid / kill EPERM rules here — they live in
 * process/signal.
 *  - Driving the scheduler outside of process_exit()/schedule() pairs.
 */

#include "syscall.h"

#include <kernel/cred.h>
#include <kernel/errno.h>
#include <kernel/jobctl.h>
#include <kernel/memory_layout.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/signal.h>
#include <kernel/uaccess.h>
#include <kernel/waitq.h>
#include <uapi/signal.h>

int32_t sys_exit(uint32_t status, uint32_t u2, uint32_t u3, uint32_t u4,
                 uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  int wstatus = (int)((status & 0xFF) << 8);
  process_exit(wstatus);
  schedule();
  __builtin_unreachable();
}

int32_t sys_getpid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->pid;
}

int32_t sys_getppid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->ppid;
}

int32_t sys_getuid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->uid;
}

int32_t sys_getgid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->gid;
}

int32_t sys_geteuid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->euid;
}

int32_t sys_getegid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->egid;
}

int32_t sys_setuid(uint32_t uid, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)cred_set_uid(get_current_process(), (uid_t)uid);
}

int32_t sys_setgid(uint32_t gid, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)cred_set_gid(get_current_process(), (gid_t)gid);
}

int32_t sys_seteuid(uint32_t euid, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)cred_set_euid(get_current_process(), (uid_t)euid);
}

int32_t sys_setegid(uint32_t egid, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)cred_set_egid(get_current_process(), (gid_t)egid);
}

static int waitpid_reap(process_t *child, uint32_t status_ptr) {
  if (status_ptr) {
    int s = child->exit_code;
    int rc = copy_to_user((void *)status_ptr, &s, sizeof(s));
    if (IS_ERROR(rc))
      return rc;
  }
  int p = child->pid;
  if (child->state == PROCESS_ZOMBIE)
    process_terminate(child);
  return p;
}

int32_t sys_waitpid(uint32_t pid, uint32_t status_ptr, uint32_t opts,
                    uint32_t u4, uint32_t u5) {
  (void)u4;
  (void)u5;

  process_t *cur = get_current_process();

  if (!process_find_child(cur->pid, (int32_t)pid, 0))
    return -ECHILD;

  if (opts & 1) {
    process_t *c = process_find_child(cur->pid, (int32_t)pid, 1);
    return c ? waitpid_reap(c, status_ptr) : 0;
  }

  process_t *child;
  while (!(child = process_find_child(cur->pid, (int32_t)pid, 1))) {
    if (!process_find_child(cur->pid, (int32_t)pid, 0))
      return -ECHILD;
    wait_event(&cur->children_wq,
               (child = process_find_child(cur->pid, (int32_t)pid, 1)) !=
                       NULL ||
                   !process_find_child(cur->pid, (int32_t)pid, 0));
  }

  return waitpid_reap(child, status_ptr);
}

int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t u3, uint32_t u4,
                 uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)process_send_signal((pid_t)pid, (int)sig);
}

int32_t sys_signal(uint32_t sig, uint32_t handler, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (sig >= NSIG || sig == 0)
    return -EINVAL;

  if (handler > 1 && (handler < USER_CODE_BASE || handler >= USER_SPACE_END)) {
    return -EFAULT;
  }

  sig_handler_t h = (sig_handler_t)(uintptr_t)handler;
  return process_set_signal_handler(get_current_process(), (int)sig, h);
}

int32_t sys_gettid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return (int32_t)get_current_process()->pid; /* Single-threaded: TID == PID. */
}

int32_t sys_set_tid_address(uint32_t tidptr, uint32_t u2, uint32_t u3,
                            uint32_t u4, uint32_t u5) {
  (void)tidptr;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  /* TODO : wire to process_t.clear_child_tid */
  return (int32_t)get_current_process()->pid;
}

extern int32_t sys_fork(void *regs_ptr);

int32_t sys_fork_wrap(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                      uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return sys_fork((void *)this_cpu()->syscall_regs);
}

int32_t sys_setpgid(uint32_t pid, uint32_t pgid, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  return process_setpgid((pid_t)pid, (pid_t)pgid);
}

int32_t sys_getpgid(uint32_t pid, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return process_getpgid((pid_t)pid);
}

int32_t sys_getpgrp(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                    uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return process_getpgrp();
}

int32_t sys_setsid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return process_setsid();
}

int32_t sys_getsid(uint32_t pid, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return process_getsid((pid_t)pid);
}

int32_t sys_tcgetpgrp(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4,
                      uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return process_tcgetpgrp((int)fd);
}

int32_t sys_tcsetpgrp(uint32_t fd, uint32_t pgrp, uint32_t u3, uint32_t u4,
                      uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  return process_tcsetpgrp((int)fd, (pid_t)pgrp);
}
