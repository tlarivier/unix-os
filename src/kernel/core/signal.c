/*
 * signal.c — Kernel signal API: per-process handler init, send with
 * saved-uid permission check and cross-CPU IPI wake, default-action
 * delivery (TERM / IGN), handler installation, and pending-signal probe.
 *
 * Invariants:
 *  - process_send_signal only sets a pending bit or a terminal state;
 *    actual ring-3 handler dispatch is not implemented (no userspace
 *    return path consumes signal_pending today).
 *  - target->state reads use __atomic_load_n(ACQUIRE) so a TERMINATED
 *    store from another CPU is observed before deciding to IPI.
 *  - Permission check follows POSIX: sender's euid must be 0, or match
 *    target's real-uid or saved-uid; otherwise -EPERM.
 *  - signal_handlers[] is sized to NSIG_HANDLED (32); senders reject
 *    signal numbers >= NSIG_HANDLED with -EINVAL.
 *
 * Not allowed:
 *  - Calling schedule() inline from process_send_signal (IPI does the
 *    wake; the target's exit-to-userspace path observes state changes).
 *  - kmalloc / vfs_* from any signal entry point.
 *  - Mutating state to anything other than TERMINATED/STOPPED/RUNNING
 *    per the scheduler invariants in north_star/core.md §5.
 */

#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/signal.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <uapi/signal.h>

enum { ACT_TERM, ACT_IGN };

static inline int default_action(int sig) {
  switch (sig) {
  case SIGCHLD:
  case SIGUSR1:
  case SIGUSR2:
    return ACT_IGN;
  default:
    return ACT_TERM;
  }
}

static int process_deliver_signal(process_t *proc, int signal);

void process_init_signals(process_t *proc) {
  if (!proc)
    return;
  proc->signal_mask = 0;
  proc->signal_pending = 0;
  for (int i = 0; i < NSIG_HANDLED; i++) {
    proc->signal_handlers[i] = SIG_DFL;
  }
}

int process_send_signal(pid_t pid, int signal) {
  if (signal < 1 || signal >= NSIG_HANDLED)
    return -EINVAL;
  process_t *target = process_find_by_pid(pid);
  if (!target)
    return -ESRCH;

  process_t *sender = get_current_process();
  if (sender && sender->euid != 0 && sender->euid != target->uid &&
      sender->euid != target->suid) {
    return -EPERM;
  }

  int ret = 0;
  int deliver = 0;
  switch (signal) {
  case SIGKILL: {
    uint32_t irq_flags = local_irq_save();
    __atomic_store_n(&target->state, PROCESS_TERMINATED, __ATOMIC_RELEASE);
    target->exit_code = SIGKILL & 0x7F;
    local_irq_restore(irq_flags);
    break;
  }
  case SIGSTOP: {
    uint32_t irq_flags = local_irq_save();
    __atomic_store_n(&target->state, PROCESS_BLOCKED, __ATOMIC_RELEASE);
    local_irq_restore(irq_flags);
    break;
  }
  case SIGCONT: {
    uint32_t expected = PROCESS_BLOCKED;
    __atomic_compare_exchange_n(&target->state, &expected, PROCESS_READY, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    break;
  }
  default:
    if (__atomic_load_n(&target->signal_mask, __ATOMIC_ACQUIRE) &
        (1U << signal)) {
      __atomic_fetch_or(&target->signal_pending, (sigset_t)(1U << signal),
                        __ATOMIC_RELEASE);
    } else {
      deliver = 1;
    }
    break;
  }

  if (deliver) {
    ret = process_deliver_signal(target, signal);
  }

  if (target->owner_cpu < cpu_count && target->owner_cpu != this_cpu()->id) {
    smp_ipi_send(target->owner_cpu, IPI_VEC_RESCHED);
  }
  return ret;
}

static int process_deliver_signal(process_t *proc, int signal) {
  if (!proc)
    return -EINVAL;

  sig_handler_t handler =
      __atomic_load_n(&proc->signal_handlers[signal], __ATOMIC_ACQUIRE);
  if (handler == SIG_IGN)
    return 0;
  if (handler == SIG_DFL) {
    if (default_action(signal) == ACT_TERM) {
      __atomic_store_n(&proc->state, PROCESS_TERMINATED, __ATOMIC_RELEASE);
      proc->exit_code = signal & 0x7F;
    }
    return 0;
  }

  __atomic_fetch_or(&proc->signal_pending, (sigset_t)(1U << signal),
                    __ATOMIC_RELEASE);
  return 0;
}

int process_set_signal_handler(process_t *proc, int signal,
                               sig_handler_t handler) {
  if (!proc || signal < 1 || signal >= NSIG_HANDLED)
    return -EINVAL;
  if (signal == SIGKILL || signal == SIGSTOP)
    return -EINVAL;

  /* Atomic swap so a delivery on another CPU observes either the old or
   * the new handler, never a torn or stale value. */
  sig_handler_t old_handler = __atomic_exchange_n(&proc->signal_handlers[signal],
                                                  handler, __ATOMIC_ACQ_REL);
  return (int)(uintptr_t)old_handler;
}

int signal_pending_check(void) {
  process_t *proc = get_current_process();
  if (!proc)
    return 0;
  return (proc->signal_pending & ~proc->signal_mask) != 0;
}
