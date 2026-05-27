/*
 * ioapic.c — program the I/O APIC once on the BSP: read acpi_info, mask
 * every RTE, route IRQ 0/1/4 to vectors 32/33/36 on LAPIC 0, then mask
 * both 8259 PICs so each IRQ has a single delivery path.
 *
 * Invariants:
 *  - Runs only on the BSP, only after lapic_init (the LAPIC MMIO mapping
 *    is the prerequisite for IOAPIC MMIO to be reachable).
 *  - On return, every IOAPIC RTE is either masked or pointed at LAPIC 0
 *    with a single vector; both PICs (0x21 and 0xA1 OCW1) are 0xFF so
 *    no IRQ can be double-delivered.
 *  - 32-bit MMIO only, via the IOREGSEL/IOWIN indirection (no byte or
 *    16-bit accesses to the IOAPIC window).
 *
 * Not allowed:
 *  - Re-run on APs or call from interrupt context.
 *  - Touch kernel_page_directory[], scheduler, VFS, kmalloc, or PIT.
 *  - Mutate cpus[] or acpi_info; both are read-only here.
 */

#include <kernel/acpi.h>
#include <kernel/io.h>
#include <kernel/kprintf.h>
#include <kernel/lapic.h>
#include <kernel/paging.h>
#include <stdint.h>

#define IOAPIC_REGSEL_OFFSET 0x00
#define IOAPIC_WIN_OFFSET 0x10

#define IOAPIC_REG_ID 0x00
#define IOAPIC_REG_VER 0x01
#define IOAPIC_REG_RTE_BASE 0x10

#define RTE_MASKED (1u << 16)

static uint32_t ioapic_phys = 0;

static inline uint32_t ioapic_read(uint32_t reg) {
  *(volatile uint32_t *)(ioapic_phys + IOAPIC_REGSEL_OFFSET) = reg;
  return *(volatile uint32_t *)(ioapic_phys + IOAPIC_WIN_OFFSET);
}

static inline void ioapic_write(uint32_t reg, uint32_t val) {
  *(volatile uint32_t *)(ioapic_phys + IOAPIC_REGSEL_OFFSET) = reg;
  *(volatile uint32_t *)(ioapic_phys + IOAPIC_WIN_OFFSET) = val;
}

static void ioapic_route(uint8_t gsi, uint8_t vec, uint8_t dest_apic_id) {
  uint32_t low = vec;
  uint32_t high = ((uint32_t)dest_apic_id) << 24;
  ioapic_write(IOAPIC_REG_RTE_BASE + 2 * gsi + 1, high);
  ioapic_write(IOAPIC_REG_RTE_BASE + 2 * gsi + 0, low);
}

static void pic_mask_all(void) {
  outb(0x21, 0xFF); /* PIC1 mask */
  outb(0xA1, 0xFF); /* PIC2 mask */
}

void ioapic_init(void) {
  if (!acpi_info.present || acpi_info.ioapic_phys_addr == 0) {
    kprintf("IOAPIC: ACPI didn't report one — keeping PIC routing\n");
    return;
  }
  ioapic_phys = acpi_info.ioapic_phys_addr;

  (void)map_uncached_mmio(ioapic_phys);

  uint32_t ver = ioapic_read(IOAPIC_REG_VER);
  uint32_t max_rte = (ver >> 16) & 0xFF;
  kprintf("IOAPIC: base=%x version=%x max_rte=%u\n", ioapic_phys, ver & 0xFF,
          max_rte);

  for (uint32_t i = 0; i <= max_rte; i++) {
    ioapic_write(IOAPIC_REG_RTE_BASE + 2 * i + 0, RTE_MASKED);
    ioapic_write(IOAPIC_REG_RTE_BASE + 2 * i + 1, 0);
  }

  ioapic_route(2, 32, 0); /* PIT timer  -> vector 32 -> BSP */
  ioapic_route(1, 33, 0); /* Keyboard   -> vector 33 -> BSP */
  ioapic_route(4, 36, 0); /* COM1       -> vector 36 -> BSP */

  pic_mask_all();

  kprintf("IOAPIC: routed IRQ 0/1/4 -> vec 32/33/36 -> LAPIC 0\n");
}
