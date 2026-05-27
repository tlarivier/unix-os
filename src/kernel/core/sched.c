/*
 * sched.c — per-CPU MLFQ scheduler with throttled work-stealing and
 *           READY/RUNNING context switch (CR3 + kernel stack load).
 *
 * Invariants:
 *  - this_cpu()->current_proc is never NULL once scheduler_enable() ran;
 *    the idle proc backstops empty local and remote runqueues.
 *  - schedule() saves/restores local IRQs across its body; inner rq_lock
 *    uses raw_spin_lock because IRQs are already masked.
 *  - proc->state mutations limited to READY <-> RUNNING (plus the initial
 *    READY enqueue); BLOCKED/ZOMBIE/TERMINATED belong to waitq/fork/signal.
 *  - proc->owner_cpu is written only by scheduler_add_process (placement)
 *    and try_steal_work (migration); rq_lock acquisition order is by id.
 *  - cpu_t.idle_proc never sits on a runqueue and is never demoted.
 *
 * Not allowed:
 *  - kmalloc / allocate_frame / heap_alloc — all sched state pre-exists.
 *  - VFS / block / pipe / sleeping locks — only raw_spin_lock on rq_lock.
 *  - kprintf on the hot path of schedule() (panic-only).
 */

#include <kernel/constants.h>
#include <kernel/gdt.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/sched_arch.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NUM_QUEUES 4
static const uint32_t queue_quantum[NUM_QUEUES] = {5, 10, 20, 40};

#define STEAL_MIN_INTERVAL 4

static inline process_t **rq_head(cpu_t *cpu, int q) {
  return (process_t **)&cpu->run_queues[q];
}

static inline void rq_lock(cpu_t *cpu) { raw_spin_lock(&cpu->rq_lock); }
static inline void rq_unlock(cpu_t *cpu) { raw_spin_unlock(&cpu->rq_lock); }
static volatile uint32_t rr_counter = 0;

static int scheduler_enabled = 0;

void scheduler_enable(void) {
  __atomic_store_n(&scheduler_enabled, 1, __ATOMIC_RELEASE);
}

void scheduler_register_idle(uint32_t cpu_id, process_t *idle) {
  if (!idle) {
    KERNEL_PANIC("scheduler_register_idle: NULL idle proc "
                 "(invariant n.6 broken)");
  }
  cpus[cpu_id].idle_proc = idle;
}

static void add_to_queue(cpu_t *cpu, process_t *proc, int queue) {
  if (queue < 0)
    queue = 0;
  if (queue >= NUM_QUEUES)
    queue = NUM_QUEUES - 1;
  proc->mlfq_queue = (int8_t)queue;

  process_t **head = rq_head(cpu, queue);
  if (!*head) {
    *head = proc;
    proc->next = proc;
  } else {
    proc->next = (*head)->next;
    (*head)->next = proc;
  }
}

static void remove_from_queue(cpu_t *cpu, process_t *proc) {
  int queue = proc->mlfq_queue;
  if (queue < 0 || queue >= NUM_QUEUES)
    return;

  process_t **head = rq_head(cpu, queue);
  if (!*head)
    return;

  if (*head == proc && proc->next == proc) {
    *head = NULL;
  } else {
    process_t *prev = *head;
    while (prev->next != proc) {
      prev = prev->next;
      if (prev == *head)
        return;
    }
    prev->next = proc->next;
    if (*head == proc)
      *head = proc->next;
  }
  proc->next = NULL;
}

void scheduler_add_process(process_t *proc) {
  if (!proc)
    return;
  if (proc->pid == 0)
    return;
  __atomic_store_n(&proc->state, PROCESS_READY, __ATOMIC_RELEASE);
  proc->time_slice = queue_quantum[0];

  uint32_t n = __atomic_fetch_add(&rr_counter, 1, __ATOMIC_RELAXED);
  uint32_t target_id = (cpu_count > 0) ? (n % cpu_count) : 0;
  cpu_t *target = &cpus[target_id];

  proc->owner_cpu = target_id;
  rq_lock(target);
  add_to_queue(target, proc, 0);
  rq_unlock(target);

  if (target_id != this_cpu()->id) {
    smp_ipi_send(target_id, IPI_VEC_RESCHED);
  }
}

void scheduler_remove_process(process_t *proc) {
  if (!proc)
    return;
  uint32_t flags = local_irq_save();
  cpu_t *owner = &cpus[proc->owner_cpu];
  rq_lock(owner);
  remove_from_queue(owner, proc);
  rq_unlock(owner);
  local_irq_restore(flags);
}

typedef struct {
  process_t *proc;
  int queue;
} sched_pick_t;

static sched_pick_t find_next_process(cpu_t *me) {
  sched_pick_t pick = {NULL, 0};
  for (int q = 0; q < NUM_QUEUES; q++) {
    process_t **head = rq_head(me, q);
    if (*head) {
      pick.proc = *head;
      pick.queue = q;
      return pick;
    }
  }
  return pick;
}

static process_t *try_steal_work(cpu_t *me) {
  uint32_t now = me->sched_ticks;
  if (now >= STEAL_MIN_INTERVAL &&
      now - me->last_steal_tick < STEAL_MIN_INTERVAL) {
    return NULL;
  }
  me->last_steal_tick = now;

  for (uint32_t i = 0; i < cpu_count; i++) {
    if (i == me->id)
      continue;
    cpu_t *v = &cpus[i];
    cpu_t *a = (me->id < v->id) ? me : v;
    cpu_t *b = (me->id < v->id) ? v : me;
    rq_lock(a);
    rq_lock(b);

    process_t *steal = NULL;
    for (int q = 0; q < NUM_QUEUES; q++) {
      process_t **head = rq_head(v, q);
      if (!*head || (*head)->next == *head)
        continue;
      steal = (*head)->next;
      (*head)->next = steal->next;
      steal->next = NULL;
      steal->owner_cpu = me->id;
      add_to_queue(me, steal, steal->mlfq_queue);
      break;
    }
    rq_unlock(b);
    rq_unlock(a);
    if (steal)
      return steal;
  }
  return NULL;
}

static process_t *pick_next_or_idle(cpu_t *me, int *next_queue) {
  sched_pick_t pick = find_next_process(me);

  if (pick.proc && pick.proc == current_process &&
      current_process->state != PROCESS_RUNNING) {
    process_t **head = rq_head(me, pick.queue);
    *head = (*head)->next;
    pick = find_next_process(me);
    if (pick.proc == current_process)
      pick.proc = NULL;
  }

  if (!pick.proc) {
    pick.proc = try_steal_work(me);
    if (pick.proc)
      pick.queue = pick.proc->mlfq_queue;
  }
  if (!pick.proc)
    pick.proc = (process_t *)me->idle_proc;

  *next_queue = pick.queue;
  return pick.proc;
}

static void demote_quantum_exhausted(cpu_t *me, process_t *cur) {
  if (!cur)
    return;
  if (cur == (process_t *)me->idle_proc)
    return;
  if (cur->state != PROCESS_RUNNING)
    return;
  if (me->quantum_left != 0)
    return;

  remove_from_queue(me, cur);
  int new_queue = cur->mlfq_queue + 1;
  if (new_queue >= NUM_QUEUES)
    new_queue = NUM_QUEUES - 1;
  __atomic_store_n(&cur->state, PROCESS_READY, __ATOMIC_RELEASE);
  add_to_queue(me, cur, new_queue);
}

void schedule(void) {
  uint32_t flags = local_irq_save();
  cpu_t *me = this_cpu();
  me->schedule_calls++;
  me->rcu_qs_count++;

  int next_queue = 0;
  process_t *next = pick_next_or_idle(me, &next_queue);
  process_t *idle = (process_t *)me->idle_proc;

  if (next == idle) {
    if (idle && current_process && current_process != idle &&
        current_process->state != PROCESS_RUNNING) {
      process_t *old = current_process;
      current_process = idle;
      __atomic_store_n(&idle->state, PROCESS_RUNNING, __ATOMIC_RELEASE);
      context_switch(old, idle);
    }
    local_irq_restore(flags);
    return;
  }

  int idle_preempt = (current_process == idle);

  if (idle_preempt || me->quantum_left == 0 || !current_process ||
      current_process->state != PROCESS_RUNNING) {
    demote_quantum_exhausted(me, current_process);
    next = pick_next_or_idle(me, &next_queue);

    if (next && next != current_process && next != idle) {
      process_t *old = current_process;
      current_process = next;
      me->current_queue = next_queue;
      __atomic_store_n(&next->state, PROCESS_RUNNING, __ATOMIC_RELEASE);
      me->quantum_left = queue_quantum[next_queue];
      process_t **head = rq_head(me, next_queue);
      *head = (*head)->next;
      if (old) {
        context_switch(old, current_process);
        return;
      }
    } else if (current_process) {
      me->quantum_left = queue_quantum[current_process->mlfq_queue];
    }
  }

  local_irq_restore(flags);
}

void timer_tick(void) {
  if (!__atomic_load_n(&scheduler_enabled, __ATOMIC_ACQUIRE))
    return;
  cpu_t *me = this_cpu();

  me->sched_ticks++;
  if (me->quantum_left > 0)
    me->quantum_left--;
}

void context_switch(process_t *old, process_t *new) {
  if (!old || !new || old == new)
    return;
  /* Only demote RUNNING→READY; never clobber a concurrent TERMINATED/ZOMBIE
   * written by signal delivery on another CPU. */
  uint32_t expected = PROCESS_RUNNING;
  __atomic_compare_exchange_n(&old->state, &expected, PROCESS_READY,
                              0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  __atomic_store_n(&new->state, PROCESS_RUNNING, __ATOMIC_RELEASE);
  current_process = new;

  if (new->memory &&new->memory->page_directory &&
      (!old->memory ||
       old->memory->page_directory != new->memory->page_directory)) {
    switch_page_directory(new->memory->page_directory);
  }
  if (new->kernel_stack) {
    tss_set_kernel_stack((uint32_t)new->kernel_stack + KERNEL_STACK_SIZE);
  }

  if (new->first_sched) {
    new->first_sched = 0;
  }

  uint32_t old_state = __atomic_load_n(&old->state, __ATOMIC_ACQUIRE);
  if (old_state == PROCESS_ZOMBIE || old_state == PROCESS_TERMINATED) {
    asm_context_switch(NULL, &new->context);
  } else {
    asm_context_switch(&old->context, &new->context);
  }
}
