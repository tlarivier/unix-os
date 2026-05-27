/*
 * rcu.c — synchronize_rcu() implementation: waits for every CPU to pass
 *         through a quiescent state since the call (read-side primitives
 *         are inline in include/kernel/rcu.h).
 *
 * Invariants:
 *  - synchronize_rcu is a global barrier, not a callback dispatcher
 *    (call_rcu was removed).
 *  - Mono-CPU degrades to a single schedule(); SMP samples rcu_qs_count
 *    then IPI + spin-wait.
 *  - rcu_qs_count is incremented only by schedule() (single producer per
 *    CPU).
 *  - A final __sync_synchronize() guarantees reader writes happen-before
 *    synchronize_rcu returns.
 *
 * Not allowed:
 *  - Blocking or sleeping between rcu_read_lock and rcu_read_unlock
 *    (preempt is disabled).
 *  - Allocating or holding a callback queue.
 */

#include <kernel/kernel.h>
#include <kernel/percpu.h>
#include <kernel/rcu.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>

void synchronize_rcu(void) {
  if (cpu_count <= 1) {
    schedule();
  } else {
    uint32_t my_id = this_cpu()->id;
    uint32_t snapshot[MAX_CPUS];
    for (uint32_t i = 0; i < cpu_count; i++) {
      snapshot[i] = __atomic_load_n(&cpus[i].rcu_qs_count, __ATOMIC_ACQUIRE);
    }
    for (uint32_t i = 0; i < cpu_count; i++) {
      if (i == my_id) {
        schedule();
        continue;
      }
      smp_ipi_send(i, IPI_VEC_RESCHED);
      while (__atomic_load_n(&cpus[i].rcu_qs_count, __ATOMIC_ACQUIRE) ==
             snapshot[i]) {
        __asm__ volatile("pause" ::: "memory");
      }
    }
  }

  __sync_synchronize();
}
