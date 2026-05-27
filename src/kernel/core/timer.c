/*
 * timer.c — HW-neutral monotonic tick counter (clocksource) consumed by
 *           scheduler, alarm scan, SMP bring-up and the time syscalls.
 *
 * Invariants:
 *  - timer_ticks and seconds are mutated only by clocksource_tick(),
 *    which runs from the timer IRQ handler at TIMER_HZ.
 *  - All counter accesses go through __atomic_* with RELAXED ordering;
 *    callers that need happens-before establish it themselves.
 *  - sleep_ms uses the canonical "sti; hlt; cli" idiom so an IRQ-gated
 *    syscall can still be woken by the timer interrupt.
 *  - The hardware driver (pit.c today, HPET/LAPIC later) owns the chip
 *    and calls clocksource_tick(); this TU owns the counters.
 *
 * Not allowed:
 *  - Touching hardware registers or the IDT — that is the driver's job.
 *  - kmalloc / VFS / sleeping locks — every routine is IRQ-safe.
 *  - Mutating any other subsystem's state from clocksource_tick().
 */

#include <kernel/timer.h>
#include <stdint.h>

static uint32_t timer_ticks = 0;
static uint32_t seconds = 0;

void clocksource_tick(void) {
  uint32_t new_ticks = __atomic_add_fetch(&timer_ticks, 1, __ATOMIC_RELAXED);
  if (new_ticks % TIMER_HZ == 0) {
    __atomic_add_fetch(&seconds, 1, __ATOMIC_RELAXED);
  }
}

uint32_t get_timer_ticks(void) {
  return __atomic_load_n(&timer_ticks, __ATOMIC_RELAXED);
}

uint32_t get_seconds(void) {
  return __atomic_load_n(&seconds, __ATOMIC_RELAXED);
}

void sleep_ms(uint32_t ms) {
  if (ms == 0)
    return;
  uint32_t start = __atomic_load_n(&timer_ticks, __ATOMIC_RELAXED);
  uint32_t ticks_to_wait = (ms * TIMER_HZ) / 1000;
  if (ticks_to_wait == 0)
    ticks_to_wait = 1;

  while ((__atomic_load_n(&timer_ticks, __ATOMIC_RELAXED) - start) <
         ticks_to_wait) {
    __asm__ volatile("sti; hlt; cli");
  }
}

int time_sleep_timespec(const struct k_timespec *req, struct k_timespec *rem) {
  if (!req)
    return -1;
  uint32_t ms =
      (uint32_t)(req->tv_sec) * 1000u + (uint32_t)(req->tv_nsec) / 1000000u;
  if (ms == 0 && req->tv_nsec > 0)
    ms = 1;
  sleep_ms(ms);
  if (rem) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}
