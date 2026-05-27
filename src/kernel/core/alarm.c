/*
 * alarm.c — per-tick O(MAX_PROCESSES) alarm scan that decrements every
 *           proc->alarm_ticks and delivers SIGALRM on the 1->0 edge.
 *
 * Invariants:
 *  - Called from the timer IRQ handler at TIMER_HZ; no blocking, no
 *    allocation, no sleeping locks anywhere on this path.
 *  - alarm_ticks is read/written with __atomic_* only — concurrent CPUs
 *    may scan in parallel; the fetch_sub catches a single 1->0 winner.
 *  - PROCESS_TERMINATED and PROCESS_ZOMBIE entries are skipped; reading
 *    a freed process_t never happens because slots are reaped under lock.
 *  - The driver layer (pit.c) holds zero knowledge of process table or
 *    signal subsystem — this TU is the isolation boundary.
 *
 * Not allowed:
 *  - kmalloc / VFS / spin_lock / preempt_disable on the tick path.
 *  - Mutating proc->state — only proc->alarm_ticks is touched here.
 *  - Calling schedule() — signal posting is asynchronous to delivery.
 */

#include <../uapi/signal.h>
#include <kernel/alarm.h>
#include <kernel/constants.h>
#include <kernel/process.h>
#include <kernel/signal.h>
#include <stdint.h>

void alarm_tick(void) {
  for (uint32_t i = 0; i < MAX_PROCESSES_CONST; i++) {
    process_t *p = process_get_by_index(i);
    if (!p)
      continue;
    uint32_t st = __atomic_load_n(&p->state, __ATOMIC_RELAXED);
    if (st == PROCESS_TERMINATED || st == PROCESS_ZOMBIE)
      continue;
    uint32_t cur = __atomic_load_n(&p->alarm_ticks, __ATOMIC_RELAXED);
    if (cur == 0)
      continue;
    uint32_t prev = __atomic_fetch_sub(&p->alarm_ticks, 1, __ATOMIC_RELAXED);
    if (prev == 1) {
      process_send_signal(p->pid, SIGALRM);
    }
  }
}
