/*
 * lapic.c — Local APIC driver: map the MMIO page uncached, software-enable
 * the calling CPU's LAPIC, and expose IPI helpers, EOI, periodic timer
 * start, and the calibration hooks consumed by lapic_calibrate.c.
 *
 * Invariants:
 *  - lapic_init returns on the calling CPU before any lapic_send_ipi /
 *    lapic_send_init / lapic_send_startup is issued (software-enable via
 *    LAPIC_SVR_ENABLE precedes every ICR write).
 *  - MMIO is mapped uncached (PCD): every load goes to the device, no
 *    stale ICR-busy or timer-count reads.
 *  - lapic_read / lapic_write stay file-static; external code uses the
 *    typed helpers (V05).
 *
 * Not allowed:
 *  - Touch kernel_page_directory[] directly (V06: mapping goes through
 *    map_uncached_mmio in mm).
 *  - Depend on the PIT or get_timer_ticks (V07: calibration lives in
 *    lapic_calibrate.c).
 *  - Call scheduler, VFS, kmalloc, or hold a spinlock.
 */

#include <kernel/acpi.h>
#include <kernel/kprintf.h>
#include <kernel/lapic.h>
#include <kernel/paging.h>
#include <kernel/percpu.h>
#include <stdint.h>

#define LAPIC_REG_ID 0x020
#define LAPIC_REG_VERSION 0x030
#define LAPIC_REG_TPR 0x080
#define LAPIC_REG_EOI 0x0B0
#define LAPIC_REG_SVR 0x0F0
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310
#define LAPIC_REG_TIMER_LVT 0x320
#define LAPIC_REG_TIMER_INIT 0x380
#define LAPIC_REG_TIMER_CURR 0x390
#define LAPIC_REG_TIMER_DIV 0x3E0

#define LAPIC_SVR_ENABLE (1u << 8)
#define LAPIC_SPURIOUS_VECTOR 0xFF

#define LAPIC_TIMER_PERIODIC (1u << 17)
#define LAPIC_TIMER_MASKED (1u << 16)

#define ICR_DELIVERY_FIXED (0x0 << 8)
#define ICR_DELIVERY_INIT (0x5 << 8)
#define ICR_DELIVERY_STARTUP (0x6 << 8)
#define ICR_LEVEL_ASSERT (1 << 14)
#define ICR_TRIGGER_EDGE (0 << 15)
#define ICR_DEST_PHYSICAL (0 << 11)
#define ICR_BUSY (1 << 12)

#define LAPIC_VIRT_BASE 0xFEE00000

static int g_mapped = 0;

static inline uint32_t lapic_read(uint32_t reg) {
  return *(volatile uint32_t *)(LAPIC_VIRT_BASE + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
  *(volatile uint32_t *)(LAPIC_VIRT_BASE + reg) = val;
}

static uint32_t lapic_get_id(void) {
  return (lapic_read(LAPIC_REG_ID) >> 24) & 0xFF;
}

static void icr_wait_idle(void) {
  while (lapic_read(LAPIC_REG_ICR_LOW) & ICR_BUSY)
    __asm__ volatile("pause" ::: "memory");
}

void lapic_send_ipi(uint32_t target_apic_id, uint8_t vector) {
  lapic_write(LAPIC_REG_ICR_HIGH, target_apic_id << 24);
  lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_FIXED | ICR_LEVEL_ASSERT |
                                     ICR_TRIGGER_EDGE | ICR_DEST_PHYSICAL |
                                     (uint32_t)vector);
  icr_wait_idle();
}

void lapic_send_init(uint32_t target_apic_id) {
  lapic_write(LAPIC_REG_ICR_HIGH, target_apic_id << 24);
  lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_INIT | ICR_LEVEL_ASSERT |
                                     ICR_TRIGGER_EDGE | ICR_DEST_PHYSICAL);
  icr_wait_idle();
}

void lapic_send_startup(uint32_t target_apic_id, uint8_t trampoline_vector) {
  lapic_write(LAPIC_REG_ICR_HIGH, target_apic_id << 24);
  lapic_write(LAPIC_REG_ICR_LOW, ICR_DELIVERY_STARTUP | ICR_LEVEL_ASSERT |
                                     ICR_TRIGGER_EDGE | ICR_DEST_PHYSICAL |
                                     (uint32_t)trampoline_vector);
  icr_wait_idle();
}

void lapic_eoi(void) { lapic_write(LAPIC_REG_EOI, 0); }

int lapic_init(void) {
  uint32_t lapic_phys =
      acpi_info.present ? acpi_info.lapic_phys_addr : LAPIC_PHYS_DEFAULT;
  if (lapic_phys == 0)
    lapic_phys = LAPIC_PHYS_DEFAULT;

  if (!g_mapped) {
    (void)map_uncached_mmio(lapic_phys);
    g_mapped = 1;
  }

  uint32_t svr = lapic_read(LAPIC_REG_SVR);
  svr = (svr & ~0xFFu) | LAPIC_SPURIOUS_VECTOR;
  svr |= LAPIC_SVR_ENABLE;
  lapic_write(LAPIC_REG_SVR, svr);

  lapic_write(LAPIC_REG_TPR, 0);

  uint32_t id = lapic_get_id();
  uint32_t ver = lapic_read(LAPIC_REG_VERSION) & 0xFF;
  kprintf("LAPIC: enabled (id=%x version=%x base=%x) on cpu%d\n", id, ver,
          lapic_phys, this_cpu()->id);

  if (acpi_info.present && id != this_cpu()->lapic_id) {
    kprintf("LAPIC: WARN cpu%d expected lapic_id=%x but read %x\n",
            this_cpu()->id, this_cpu()->lapic_id, id);
  }
  return 0;
}

void lapic_timer_calibrate_setup(void) {
  lapic_write(LAPIC_REG_TIMER_LVT, LAPIC_TIMER_MASKED | LAPIC_TIMER_VECTOR);
  lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
  lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
}

void lapic_timer_calibrate_reload(void) {
  lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
}

uint32_t lapic_timer_calibrate_remaining(void) {
  return lapic_read(LAPIC_REG_TIMER_CURR);
}

void lapic_timer_calibrate_stop(void) { lapic_write(LAPIC_REG_TIMER_INIT, 0); }

void lapic_timer_start(uint32_t initial_count) {
  lapic_write(LAPIC_REG_TIMER_LVT, LAPIC_TIMER_MASKED | LAPIC_TIMER_VECTOR);
  lapic_write(LAPIC_REG_TIMER_DIV, 0x3);
  lapic_write(LAPIC_REG_TIMER_INIT, initial_count);
  lapic_write(LAPIC_REG_TIMER_LVT, LAPIC_TIMER_PERIODIC | LAPIC_TIMER_VECTOR);
}
