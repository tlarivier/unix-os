/*
 * process.c — process_t lifecycle: PID allocator, process_table[] +
 * hash, kernel_proc slot, process_create / process_exit /
 * process_terminate, and PID/index/child lookup helpers.
 *
 * Invariants:
 *  - next_pid is monotonically increasing (__atomic_fetch_add); PID 0
 *    is reserved for kernel_proc.
 *  - process_table[] and process_table_ht are mutated only under
 *    proc_table_lock; no kmalloc/kfree/vfs call while it is held.
 *  - process_t is kmalloc(sizeof(process_t)); kernel_stack is a fixed
 *    kmalloc(KERNEL_STACK_SIZE) freed at process_terminate.
 *  - State writes follow READY <-> RUNNING <-> BLOCKED -> ZOMBIE ->
 *    TERMINATED; writes use __atomic_store_n / CAS, never plain stores.
 *  - process_terminate is the sole reaper: it drops hash + slot under
 *    the lock, then kfree(proc) outside the lock.
 *
 * Not allowed:
 *  - Reading current_process without preempt_disable / IRQ-off.
 *  - Mutating proc->state outside the documented sites here, in the
 *    scheduler, in waitq, or in signal delivery.
 *  - Holding proc_table_lock across heap, vfs, or waitq calls.
 */

#include <kernel/constants.h>
#include <kernel/hashtable.h>
#include <kernel/kernel.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/waitq.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/signal.h>

static hash_table_t process_table_ht;
static process_t *process_table[MAX_PROCESSES_CONST];
static uint32_t next_pid = 1;
static process_t kernel_proc;
static spinlock_t proc_table_lock = SPINLOCK_INIT("proc_table");

pid_t allocate_next_pid(void) {
  return (pid_t)__atomic_fetch_add(&next_pid, 1, __ATOMIC_RELAXED);
}

spinlock_t *process_table_lock_get(void) { return &proc_table_lock; }
process_t **process_table_get(void) { return process_table; }
hash_table_t *process_table_ht_get(void) { return &process_table_ht; }

void process_init(void) {
  hash_table_init(&process_table_ht, "processes");
  kernel_proc.pid = 0;
  __atomic_store_n(&kernel_proc.state, PROCESS_RUNNING, __ATOMIC_RELEASE);
  kernel_proc.priority = 1;
  kernel_proc.pgid = 0;
  kernel_proc.sid = 0;
  kernel_proc.tty = -1;
  kernel_proc.umask = 022;
  kstrncpy(kernel_proc.name, "kernel", sizeof(kernel_proc.name));

  kernel_proc.kernel_stack = kmalloc(KERNEL_STACK_SIZE);
  if (kernel_proc.kernel_stack) {
    kernel_proc.context.esp =
        (uint32_t)kernel_proc.kernel_stack + KERNEL_STACK_SIZE;
  }
  kernel_proc.context.eflags = 0x202;
  kernel_proc.memory = NULL;

  process_table[0] = &kernel_proc;
  hash_table_insert(&process_table_ht, 0, &kernel_proc, &kernel_proc.ht_node);
  current_process = &kernel_proc;
}

process_t *process_create(const char *name, void *entry_point) {
  if (!name)
    return NULL;

  process_t *proc = (process_t *)kmalloc(sizeof(process_t));
  if (!proc)
    return NULL;
  kmemset(proc, 0, sizeof(process_t));

  spin_lock(&proc_table_lock);
  int slot = -1;
  for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
    if (process_table[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    spin_unlock(&proc_table_lock);
    kfree(proc);
    return NULL;
  }
  process_table[slot] = proc;
  spin_unlock(&proc_table_lock);

  proc->pid = allocate_next_pid();

  preempt_disable();
  process_t *parent_snap = current_process;
  pid_t parent_pid = parent_snap ? parent_snap->pid : 0;
  pid_t parent_sid = parent_snap ? parent_snap->sid : proc->pid;
  int parent_tty = parent_snap ? parent_snap->tty : -1;
  uint32_t parent_umask = parent_snap ? parent_snap->umask : 022;
  int parent_has_cwd = parent_snap && parent_snap->cwd[0];
  char parent_cwd[256];
  if (parent_has_cwd) {
    kstrncpy(parent_cwd, parent_snap->cwd, sizeof(parent_cwd));
  }
  preempt_enable();

  proc->ppid = parent_pid;
  __atomic_store_n(&proc->state, PROCESS_READY, __ATOMIC_RELEASE);
  proc->uid = proc->gid = 0;
  kstrncpy(proc->name, name, sizeof(proc->name));
  proc->context.eip = (uint32_t)entry_point;
  proc->context.eflags = 0x202;
  proc->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
  if (!proc->kernel_stack) {
    spin_lock(&proc_table_lock);
    process_table[slot] = NULL;
    spin_unlock(&proc_table_lock);
    kfree(proc);
    return NULL;
  }
  proc->context.esp = (uint32_t)proc->kernel_stack + KERNEL_STACK_SIZE;
  if (entry_point) {
    proc->memory = create_process_memory();
  } else {
    proc->memory = NULL;
  }
  process_init_signals(proc);
  process_init_priority(proc);

  proc->pgid = proc->pid;
  proc->sid = parent_sid;
  proc->tty = parent_tty;
  proc->umask = parent_umask;

  if (parent_has_cwd) {
    kstrncpy(proc->cwd, parent_cwd, sizeof(proc->cwd));
  } else {
    proc->cwd[0] = '/';
    proc->cwd[1] = '\0';
  }

  wait_queue_init(&proc->children_wq, "children_wq");

  spin_lock(&proc_table_lock);
  hash_table_insert(&process_table_ht, proc->pid, proc, &proc->ht_node);
  spin_unlock(&proc_table_lock);

  return proc;
}

void process_terminate(process_t *proc) {
  if (!proc || proc->pid == 0)
    return;

  __atomic_store_n(&proc->state, PROCESS_TERMINATED, __ATOMIC_RELEASE);
  if (proc->kernel_stack) {
    kfree(proc->kernel_stack);
    proc->kernel_stack = NULL;
  }

  if (proc->memory) {
    destroy_process_memory(proc->memory);
    proc->memory = NULL;
  }
  spin_lock(&proc_table_lock);
  hash_table_remove(&process_table_ht, proc->pid, NULL);
  for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
    if (process_table[i] == proc) {
      process_table[i] = NULL;
      break;
    }
  }
  spin_unlock(&proc_table_lock);
  kfree(proc);
}

void process_exit(int exit_code) {
  preempt_disable();
  process_t *me = current_process;
  if (!me || me->pid == 0) {
    preempt_enable();
    return;
  }

  pid_t my_ppid = me->ppid;
  me->exit_code = exit_code;
  if (my_ppid) {
    process_t *parent = process_find_by_pid(my_ppid);
    if (parent)
      wake_all(&parent->children_wq);
    process_send_signal(my_ppid, SIGCHLD);
  }
  __atomic_store_n(&me->state, PROCESS_ZOMBIE, __ATOMIC_RELEASE);

  scheduler_remove_process(me);
  preempt_enable();
}

process_t *get_current_process(void) {
  return current_process ? current_process : &kernel_proc;
}

void process_switch(process_t *next) {
  if (next)
    current_process = next;
}

process_t *process_find_by_pid(uint32_t pid) {
  spin_lock(&proc_table_lock);
  process_t *proc = (process_t *)hash_table_lookup(&process_table_ht, pid);
  int dead = proc && proc->state == PROCESS_TERMINATED;
  spin_unlock(&proc_table_lock);
  return dead ? NULL : proc;
}

process_t *process_get_by_index(uint32_t idx) {
  if (idx >= MAX_PROCESSES_CONST)
    return NULL;
  spin_lock(&proc_table_lock);
  process_t *p = process_table[idx];
  int dead = p && p->state == PROCESS_TERMINATED;
  spin_unlock(&proc_table_lock);
  return dead ? NULL : p;
}

process_t *process_find_child(uint32_t ppid, int32_t pid, int zombie_only) {
  process_t *p = NULL;
  spin_lock(&proc_table_lock);
  if (pid > 0) {
    process_t *q =
        (process_t *)hash_table_lookup(&process_table_ht, (uint32_t)pid);
    if (q && q->ppid == (pid_t)ppid && q->state != PROCESS_TERMINATED) {
      if (!zombie_only || q->state == PROCESS_ZOMBIE)
        p = q;
    }
  } else {
    for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
      process_t *q = process_table[i];
      if (q && q->ppid == (pid_t)ppid && q->state != PROCESS_TERMINATED) {
        if (!zombie_only || q->state == PROCESS_ZOMBIE) {
          p = q;
          break;
        }
      }
    }
  }
  spin_unlock(&proc_table_lock);
  return p;
}

