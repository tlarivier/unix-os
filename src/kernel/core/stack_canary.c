/*
 * stack_canary.c — install and verify the per-process kernel-stack
 * canary placed at the base of proc->kernel_stack.
 *
 * Invariants:
 *  - The canary word lives at the lowest address of the kernel stack;
 *    its expected value is held in proc->canary_expected.
 *  - process_init_canary writes both the in-stack word and the
 *    expected field; both are derived from rdtsc, the PID, and
 *    STACK_CANARY_PROCESS_MAGIC.
 *  - process_check_current_canary reads via get_current_process(); a
 *    mismatch is unrecoverable and routes through KERNEL_PANIC.
 *  - All stack-word reads/writes go through a volatile pointer so the
 *    compiler cannot elide or reorder them.
 *
 * Not allowed:
 *  - Allocating, freeing, or taking spinlocks: callers run in syscall
 *    entry/exit, IRQ context, and the fork path.
 *  - Mutating any process_t field other than canary_expected.
 *  - Returning on canary mismatch — the only exit is KERNEL_PANIC.
 */

#include <kernel/constants.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/random.h>
#include <stddef.h>
#include <stdint.h>

void process_init_canary(process_t *p) {
  if (!p || !p->kernel_stack)
    return;
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  uint32_t canary = STACK_CANARY_PROCESS_MAGIC ^ p->pid ^ lo;

  p->canary_expected = canary;
  *((volatile uint32_t *)p->kernel_stack) = canary;
}

void process_check_current_canary(void) {
  process_t *p = get_current_process();
  if (!p || !p->kernel_stack)
    return;

  uint32_t val = *((volatile uint32_t *)p->kernel_stack);
  if (val != p->canary_expected) {
    KERNEL_PANIC("Stack canary corrupted");
  }
}
