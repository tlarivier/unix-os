/*
 * fork.c — sys_fork: clone the caller into a fresh process_t (copy of
 * the syscall frame on the child kernel stack, clone of memory map,
 * fd_table incref, signal handlers inherit).
 *
 * Invariants:
 *  - PID is acquired via allocate_next_pid() (atomic); never bumped here.
 *  - Slot scan + process_table[] write + hash_table_insert all happen
 *    under proc_table_lock_get(); no kmalloc/kfree/vfs while holding it.
 *  - Child kernel_stack is kmalloc(KERNEL_STACK_SIZE); freed by
 *    fork_cleanup on error, by process_terminate on reap.
 *  - fd inheritance goes through of_idx_decode + vfs_open_file_incref;
 *    no direct vfs_open / vfs_read from this TU.
 *  - Initial child state is READY; only the scheduler flips it to
 *    RUNNING. fork.c never writes RUNNING or TERMINATED.
 *
 * Not allowed:
 *  - Calling vfs_* directly for fd inheritance.
 *  - Holding proc_table_lock across kmalloc, kfree, or clone_process_memory.
 *  - Mutating child->state to anything other than the initial READY.
 */

#include <kernel/constants.h>
#include <kernel/errno.h>
#include <kernel/hashtable.h>
#include <kernel/kernel.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/sched_arch.h>
#include <kernel/scheduler.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <kernel/waitq.h>
#include <stddef.h>
#include <stdint.h>

extern pid_t allocate_next_pid(void);
extern spinlock_t *process_table_lock_get(void);
extern process_t **process_table_get(void);
extern hash_table_t *process_table_ht_get(void);

static void fork_cleanup(process_t *child) {
  if (child->memory)
    destroy_process_memory(child->memory);
  if (child->kernel_stack)
    kfree(child->kernel_stack);
  kfree(child);
}

int32_t sys_fork(void *regs_ptr) {
  process_t *parent = get_current_process();
  if (!parent)
    return -ESRCH;

  if (parent->pid == 0) {
    kprintf("FORK: kernel (PID 0) cannot fork!\n");
    return -EPERM;
  }

  typedef struct {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
  } regs_t;
  regs_t *regs = (regs_t *)regs_ptr;

  process_t *child = (process_t *)kmalloc(sizeof(process_t));
  if (!child)
    return -ENOMEM;

  kmemset(child, 0, sizeof(process_t));

  child->pid = allocate_next_pid();
  child->ppid = parent->pid;
  child->uid = parent->uid;
  child->gid = parent->gid;
  __atomic_store_n(&child->state, PROCESS_READY, __ATOMIC_RELEASE);
  child->priority = parent->priority;
  child->time_slice = parent->time_slice;
  child->signal_mask = parent->signal_mask;
  child->umask = parent->umask;

  kstrncpy(child->name, parent->name, sizeof(child->name));
  kstrncpy(child->cwd, parent->cwd, sizeof(child->cwd));

  if (!parent->kernel_stack) {
    kprintf("FORK ERROR: parent PID %d has no kernel_stack!\n", parent->pid);
    fork_cleanup(child);
    return -EINVAL;
  }

  child->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
  if (!child->kernel_stack) {
    fork_cleanup(child);
    return -ENOMEM;
  }
  kmemcpy(child->kernel_stack, parent->kernel_stack, KERNEL_STACK_SIZE);

  process_init_canary(child);

  if (regs) {
    uint32_t parent_stack_base = (uint32_t)parent->kernel_stack;
    uint32_t parent_stack_top = parent_stack_base + KERNEL_STACK_SIZE;
    uint32_t regs_addr = (uint32_t)regs;

    if (regs_addr < parent_stack_base || regs_addr >= parent_stack_top) {
      kprintf("FORK: regs %x not in stack [%x-%x]\n", regs_addr,
              parent_stack_base, parent_stack_top);
      fork_cleanup(child);
      return -EINVAL;
    }

    uint32_t regs_offset = regs_addr - parent_stack_base;
    regs_t *child_regs =
        (regs_t *)((uint32_t)child->kernel_stack + regs_offset);

    child_regs->eax = 0;

    child_regs->cs = USER_CODE_SEL;
    child_regs->ss = USER_DATA_SEL;
    child_regs->ds = USER_DATA_SEL;
    child->context.eip = (uint32_t)fork_child_return;
    child->context.esp = (uint32_t)child_regs;
    child->context.eflags = 0x202;
    child->context.cs = KERNEL_CODE_SEL;
    child->context.ds = child->context.es = child->context.fs =
        child->context.gs = child->context.ss = KERNEL_DATA_SEL;
  } else {
    child->context = parent->context;
    child->context.eax = 0;
  }

  child->first_sched = 1;

  if (parent->memory) {
    child->memory = clone_process_memory(parent->memory);
    if (!child->memory) {
      fork_cleanup(child);
      return -ENOMEM;
    }
  }

  for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
    child->fd_table[i] = parent->fd_table[i];
    if (parent->fd_table[i].of_idx != 0 || parent->fd_table[i].flags != 0) {
      parent->fd_table[i].refcount++;
      child->fd_table[i].refcount = 1;
      int raw = of_idx_decode(parent->fd_table[i].of_idx);
      if (raw >= 0)
        vfs_open_file_incref(raw);
    }
  }

  for (int i = 0; i < NSIG_HANDLED; i++)
    child->signal_handlers[i] = parent->signal_handlers[i];

  wait_queue_init(&child->children_wq, "children_wq");

  child->next = NULL;

  spinlock_t *lock = process_table_lock_get();
  process_t **table = process_table_get();
  hash_table_t *ht = process_table_ht_get();
  spin_lock(lock);
  int slot = -1;
  for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
    if (table[i] == NULL) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    spin_unlock(lock);
    fork_cleanup(child);
    return -EAGAIN;
  }
  table[slot] = child;
  hash_table_insert(ht, child->pid, child, &child->ht_node);
  spin_unlock(lock);

  scheduler_add_process(child);

  return child->pid;
}
