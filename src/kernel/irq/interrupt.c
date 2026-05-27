/*
 * interrupt.c — Dispatch CPU exceptions (vector 14: COW / demand-fault /
 * userspace kill / kernel panic) and hardware/LAPIC/IPI IRQs to the
 * irq_handlers[] table that drivers populate via irq_register().
 *
 * Invariants:
 *  - Handlers run with IF=0 (interrupt gate 0x8E); no IRQ re-entrance.
 *  - %gs on entry points at the per-CPU descriptor (loaded by the asm stub
 *    from LAPIC ID); this_cpu() is usable from the first C instruction.
 *  - Exactly one LAPIC EOI is issued per hardware IRQ and per IPI; CPU
 *    exceptions never EOI; the 8259s never EOI (IOAPIC-only mode).
 *  - The irq_handlers[256] table is the only dispatch surface; drivers
 *    register before sti and never mutate it from IRQ context.
 *
 * Not allowed:
 *  - kmalloc / slub_alloc / any dynamic allocator inside a handler.
 *  - Blocking I/O, vfs_*, block_*, or any sleep / completion wait.
 *  - schedule() from IRQ context (tolerated only on the userspace-segfault
 *    tear-down path; documented technical debt).
 */

#include <kernel/error_handler.h>
#include <kernel/interrupt.h>
#include <kernel/kernel.h>
#include <kernel/lapic.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <stdint.h>
#include <uapi/signal.h>

static irq_handler_fn irq_handlers[256];

int irq_register(int vector, irq_handler_fn fn) {
  if (vector < 0 || vector >= 256)
    return -1;
  irq_handlers[vector] = fn;
  return 0;
}

void isr_handler(registers_t *regs) {
  if (regs->int_no == 14) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    uint32_t err_code = regs->err_code;

    if ((err_code & 0x01) && (err_code & 0x02)) {
      process_t *cur = get_current_process();
      page_directory_t *pd =
          (cur && cur->memory) ? cur->memory->page_directory : NULL;
      if (pd && handle_cow_fault(pd, cr2) == 0)
        return;
    }

    if (!(err_code & 0x01)) {
      if (handle_demand_fault(cr2) == 0) {
        return;
      }
    }
  }

  if ((regs->cs & 0x3) == 3) {
    process_t *cur = get_current_process();
    if (cur && cur->pid != 0) {
      process_exit(SIGSEGV & 0x7F);
      schedule();
      while (1)
        __asm__ volatile("hlt");
    }
    while (1)
      __asm__ volatile("hlt");
  }

  error_dump_and_halt(regs);
}

void irq_handler(registers_t *regs) {
  if (regs->int_no == LAPIC_TIMER_VECTOR) {
    timer_tick();
    lapic_eoi();
    return;
  }

  if (regs->int_no >= 0xFB && regs->int_no <= 0xFD) {
    ipi_dispatch(regs->int_no);
    return;
  }

  if (regs->int_no < 32 || regs->int_no > 47)
    return;

  irq_handler_fn fn = irq_handlers[regs->int_no];
  if (fn)
    fn();

  lapic_eoi();
}
