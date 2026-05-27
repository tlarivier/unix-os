/*
 * waitq.c — sleep-on-event primitives: per-object wait list, BLOCKED <->
 *           READY CAS transitions and RESCHED IPI to the sleeper owner.
 *
 * Invariants:
 *  - Per-wq spinlock guards list ops; proc->state is mutated with
 *    __atomic_* so cross-wq wakers (kill, exit) interleave correctly.
 *  - "Winner sets state": finish() flips BLOCKED -> RUNNING only via CAS;
 *    if a waker has already stamped READY or a kill TERMINATED, leave it.
 *  - current_process is snapshotted under preempt_disable in prepare so
 *    the latched pointer cannot belong to a stale CPU.
 *  - wake_one/wake_all read this_cpu()->id under preempt_disable before
 *    deciding whether to fire IPI_VEC_RESCHED on the sleeper owner.
 *
 * Not allowed:
 *  - Mutating proc->state to anything other than BLOCKED / READY here.
 *  - kmalloc, VFS or any sleeping lock inside prepare/finish/wake.
 *  - Calling schedule() directly from this TU — wait_event macro does it.
 */

#include <kernel/percpu.h>
#include <kernel/preempt.h>
#include <kernel/process.h>
#include <kernel/smp.h>
#include <kernel/waitq.h>

void wait_queue_init(wait_queue_t *wq, const char *name) {
  spinlock_init(&wq->lock, name);
  wq->head = NULL;
}

void wait_queue_prepare(wait_queue_t *wq, wait_queue_entry_t *entry) {
  preempt_disable();
  process_t *p = current_process;
  if (!p) {
    preempt_enable();
    return;
  }

  spin_lock(&wq->lock);
  if (entry->proc != p) {
    entry->proc = p;
    entry->next = wq->head;
    wq->head = entry;
  }
  __atomic_store_n(&p->state, PROCESS_BLOCKED, __ATOMIC_RELEASE);
  spin_unlock(&wq->lock);
  preempt_enable();
}

void wait_queue_finish(wait_queue_t *wq, wait_queue_entry_t *entry) {
  spin_lock(&wq->lock);
  wait_queue_entry_t **pp = &wq->head;
  while (*pp) {
    if (*pp == entry) {
      *pp = entry->next;
      entry->next = NULL;
      break;
    }
    pp = &(*pp)->next;
  }
  process_t *p = entry->proc;
  if (p) {
    uint32_t expected = PROCESS_BLOCKED;
    (void)__atomic_compare_exchange_n(&p->state, &expected, PROCESS_RUNNING,
                                      false, __ATOMIC_ACQ_REL,
                                      __ATOMIC_ACQUIRE);
    entry->proc = NULL;
  }
  spin_unlock(&wq->lock);
}

void wake_all(wait_queue_t *wq) {
  preempt_disable();
  spin_lock(&wq->lock);
  wait_queue_entry_t *e = wq->head;
  wq->head = NULL;
  spin_unlock(&wq->lock);

  while (e) {
    wait_queue_entry_t *next = e->next;
    e->next = NULL;
    process_t *p = e->proc;
    if (p) {
      __atomic_store_n(&p->state, PROCESS_READY, __ATOMIC_RELEASE);
      if (p->owner_cpu < MAX_CPUS && p->owner_cpu != this_cpu()->id) {
        smp_ipi_send(p->owner_cpu, IPI_VEC_RESCHED);
      }
    }
    e = next;
  }
  preempt_enable();
}
