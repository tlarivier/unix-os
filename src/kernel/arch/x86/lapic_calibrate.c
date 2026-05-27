/*
 * lapic_calibrate.c — measure how many LAPIC timer ticks fit in one PIT
 * tick (10 ms) and publish the result in g_lapic_timer_initial for every
 * AP's lapic_timer_start.
 *
 * Invariants:
 *  - Called exactly once, on the BSP, after both lapic_init and the PIT
 *    are online and before APs are brought up.
 *  - Drives IRQs on (sti) only inside this function and restores cli
 *    before returning; the caller observes no interrupt-state change.
 *  - Uses lapic_timer_calibrate_setup/_reload/_remaining/_stop as the
 *    only LAPIC interface; no direct lapic_read/lapic_write here.
 *
 * Not allowed:
 *  - Run on an AP or after SMP bringup has started.
 *  - Touch kernel_page_directory[], scheduler, VFS, or kmalloc.
 *  - Hold a spinlock across the sti/cli window.
 */

#include <kernel/lapic.h>
#include <kernel/smp.h>
#include <kernel/timer.h>
#include <stdint.h>

uint32_t g_lapic_timer_initial = 0;

uint32_t lapic_timer_calibrate(void) {
  lapic_timer_calibrate_setup();

  __asm__ volatile("sti");

  uint32_t start = get_timer_ticks();
  while (get_timer_ticks() == start) {
    __asm__ volatile("pause" ::: "memory");
  }
  lapic_timer_calibrate_reload();
  uint32_t base = get_timer_ticks();

  while (get_timer_ticks() == base) {
    __asm__ volatile("pause" ::: "memory");
  }
  uint32_t remaining = lapic_timer_calibrate_remaining();

  __asm__ volatile("cli");

  lapic_timer_calibrate_stop();

  return 0xFFFFFFFFu - remaining;
}
