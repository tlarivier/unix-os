/*
 * priority.c — fork-time init of proc->priority / proc->time_slice and
 *              the nice/setpriority/getpriority syscalls.
 *
 * Invariants:
 *  - process_init_priority is the single in-tree writer of the initial
 *    priority (20 = nice 0) and time_slice (100) on a new process_t.
 *  - sys_nice clamps the result to the POSIX [0, 39] range before
 *    writing proc->priority and returns the signed nice value (-20..19).
 *  - This TU writes proc->priority only; the MLFQ band (proc->mlfq_queue)
 *    is owned by sched.c and never touched from here.
 *
 * Not allowed:
 *  - Reading or writing scheduler runqueue state, owner_cpu, or quanta.
 *  - kmalloc / VFS / sleeping locks — these are constant-time setters.
 *  - Mutating proc->state — priority is independent of READY/RUNNING.
 */

#include <kernel/errno.h>
#include <kernel/process.h>

void process_init_priority(process_t *proc) {
  if (!proc)
    return;
  proc->priority = 20;
  proc->time_slice = 100;
}

int sys_nice(int increment) {
  process_t *p = get_current_process();
  if (!p)
    return -ESRCH;
  int new_prio = (int)p->priority + increment;
  if (new_prio < 0)
    new_prio = 0;
  if (new_prio > 39)
    new_prio = 39;
  p->priority = (uint32_t)new_prio;
  return new_prio - 20;
}
