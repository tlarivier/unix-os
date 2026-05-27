/*
 * jobctl.c — POSIX job-control core primitives: setpgid/getpgid/getpgrp,
 *            setsid/getsid, tcgetpgrp/tcsetpgrp on process_t fields.
 *
 * Invariants:
 *  - Pure manipulation of pid / pgid / sid / tty on a process_t; the
 *    file owns no scheduling, no locking and no syscall conventions.
 *  - sys_proc.c is the only adapter that turns 5-arg syscall frames into
 *    these clean signatures; this TU is callable from any kernel path.
 *  - console_pgrp is the single in-tree owner of the foreground pgrp for
 *    the current single-console session (until TTY unification).
 *
 * Not allowed:
 *  - Accessing the syscall ABI (u1..u5, copy_*_user) directly here.
 *  - Mutating process state other than the job-control fields above.
 *  - Holding proc_table_lock across kmalloc, VFS or signal delivery.
 */

#include <kernel/errno.h>
#include <kernel/jobctl.h>
#include <kernel/process.h>

int process_setpgid(pid_t pid, pid_t pgid) {
  process_t *cur = get_current_process();
  if (!cur)
    return -ESRCH;

  if (pid == 0)
    pid = cur->pid;
  if (pgid == 0)
    pgid = pid;

  process_t *target = process_find_by_pid(pid);
  if (!target)
    return -ESRCH;

  if (target->pid != cur->pid && target->ppid != cur->pid) {
    return -EPERM;
  }

  target->pgid = pgid;
  return 0;
}

pid_t process_getpgid(pid_t pid) {
  process_t *cur = get_current_process();
  if (!cur)
    return -ESRCH;
  if (pid == 0)
    return cur->pgid;
  process_t *target = process_find_by_pid(pid);
  if (!target)
    return -ESRCH;
  return target->pgid;
}

pid_t process_getpgrp(void) {
  process_t *cur = get_current_process();
  return cur ? cur->pgid : -ESRCH;
}

pid_t process_setsid(void) {
  process_t *cur = get_current_process();
  if (!cur)
    return -ESRCH;

  if (cur->pid == cur->pgid)
    return -EPERM;

  cur->sid = cur->pid;
  cur->pgid = cur->pid;
  cur->tty = -1;
  return cur->sid;
}

pid_t process_getsid(pid_t pid) {
  process_t *cur = get_current_process();
  if (!cur)
    return -ESRCH;
  if (pid == 0)
    return cur->sid;
  process_t *target = process_find_by_pid(pid);
  if (!target)
    return -ESRCH;
  return target->sid;
}

pid_t process_tcgetpgrp(int fd) {
  if (fd > 2)
    return -ENOTTY;
  return console_pgrp;
}

int process_tcsetpgrp(int fd, pid_t pgrp) {
  if (fd > 2)
    return -ENOTTY;
  console_pgrp = pgrp;
  return 0;
}

pid_t console_pgrp = 1;
