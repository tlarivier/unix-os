/*
 * pit.c — 8254 PIT channel-0 hardware driver: programs the chip at TIMER_HZ
 * and wires IRQ0 to clocksource_tick(); the generic counters and clock-source
 * API live in kernel/core/timer.c.
 *
 * Invariants:
 *  - PIT port programming uses outb on PIT_CMD/PIT_CH0_DATA via <kernel/io.h>.
 *  - timer_handle_irq runs in IRQ context and delegates: clocksource_tick()
 *    bumps the atomic counters, alarm_tick() drives SIGALRM, timer_tick()
 *    hooks the scheduler. The PIT TU owns none of those subsystems.
 *  - timer_init is called once at boot on the BSP, before AP bring-up and
 *    before SMP IRQ steering is enabled.
 *  - The PIT is the wall-clock master; LAPIC timer provides per-CPU preempt.
 *
 * Not allowed:
 *  - Calling process_*, signal_*, vfs_* from this TU.
 *  - Reading or mutating timer_ticks/seconds directly (owned by core/timer.c).
 *  - Holding any lock across outb; the IRQ handler must remain wait-free.
 */

#include <kernel/alarm.h>
#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <kernel/pit.h>
#include <kernel/ports.h>
#include <kernel/scheduler.h>
#include <kernel/timer.h>
#include <stdint.h>

static void timer_handle_irq(void);

void timer_init(uint32_t frequency) {
  uint32_t divisor = 1193180 / frequency;

  outb(PIT_CMD, 0x36);
  outb(PIT_CH0_DATA, divisor & 0xFF);
  outb(PIT_CH0_DATA, (divisor >> 8) & 0xFF);

  irq_register(IRQ_TIMER, timer_handle_irq);
}

static void timer_handle_irq(void) {
  clocksource_tick();
  alarm_tick();
  timer_tick();
}
