/*
 * acpi.c — scan the BIOS area for the RSDP, validate and parse
 * RSDT/XSDT then MADT, and publish in acpi_info the LAPIC/IOAPIC
 * topology discovered for the rest of the kernel to consume.
 *
 * Invariants:
 *  - Called once on the BSP after paging is on, before lapic_init; runs
 *    in protected mode and only reads memory already covered by the
 *    identity map (low BIOS [0xE0000,0xFFFFF), SDTs under 32 MiB).
 *  - No MMIO access: the file derives physical addresses (LAPIC base,
 *    IOAPIC base) but never reads or writes 0xFEC0xxxx / 0xFEE0xxxx.
 *  - acpi_info is populated solely by acpi_init; consumers (smp.c,
 *    lapic.c, ioapic.c) treat it as read-only after init returns.
 *
 * Not allowed:
 *  - Mutate cpus[] or cpu_count (V10: that is smp_init's job).
 *  - Call scheduler, VFS, mm allocators, or kernel_panic.
 *  - Leak struct acpi_rsdp / acpi_sdt_header / acpi_madt_* outside this
 *    TU (private to acpi_tables.h).
 */

#include "acpi_tables.h"
#include <kernel/acpi.h>
#include <kernel/kprintf.h>
#include <stddef.h>
#include <stdint.h>

acpi_info_t acpi_info;

static const char RSDP_SIG[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};

static int sig_eq(const char *a, const char *b, int n) {
  for (int i = 0; i < n; i++)
    if (a[i] != b[i])
      return 0;
  return 1;
}

static uint8_t checksum_bytes(const uint8_t *p, uint32_t n) {
  uint8_t s = 0;
  for (uint32_t i = 0; i < n; i++)
    s += p[i];
  return s;
}

static const acpi_rsdp_t *scan_for_rsdp(uintptr_t start, uintptr_t end) {
  for (uintptr_t p = start & ~0xFul; p + 20 <= end; p += 16) {
    const acpi_rsdp_t *cand = (const acpi_rsdp_t *)p;
    if (!sig_eq(cand->signature, RSDP_SIG, 8))
      continue;
    if (checksum_bytes((const uint8_t *)cand, 20) != 0)
      continue;
    return cand;
  }
  return NULL;
}

static const acpi_rsdp_t *find_rsdp(void) {
  uint16_t ebda_seg;
  __asm__ volatile("movw 0x40E, %0" : "=r"(ebda_seg));
  if (ebda_seg) {
    uintptr_t ebda = ((uintptr_t)ebda_seg) << 4;
    const acpi_rsdp_t *r = scan_for_rsdp(ebda, ebda + 0x400);
    if (r)
      return r;
  }
  return scan_for_rsdp(0xE0000, 0x100000);
}

static const acpi_sdt_header_t *validate_sdt(const acpi_sdt_header_t *sdt,
                                             const char *sig) {
  if (!sdt)
    return NULL;
  if (!sig_eq(sdt->signature, sig, 4))
    return NULL;
  if (checksum_bytes((const uint8_t *)sdt, sdt->length) != 0)
    return NULL;
  return sdt;
}

static const acpi_madt_t *find_madt(const acpi_sdt_header_t *root,
                                    int is_xsdt) {
  uint32_t entries_bytes = root->length - sizeof(acpi_sdt_header_t);
  int entry_size = is_xsdt ? 8 : 4;
  int num_entries = entries_bytes / entry_size;

  const uint8_t *ptrs = (const uint8_t *)root + sizeof(acpi_sdt_header_t);
  for (int i = 0; i < num_entries; i++) {
    uintptr_t addr;
    if (is_xsdt) {
      uint64_t a;
      __builtin_memcpy(&a, ptrs + i * 8, 8);
      if (a >> 32)
        continue; /* doesn't fit in 32-bit */
      addr = (uintptr_t)a;
    } else {
      uint32_t a;
      __builtin_memcpy(&a, ptrs + i * 4, 4);
      addr = (uintptr_t)a;
    }
    const acpi_sdt_header_t *sdt = (const acpi_sdt_header_t *)addr;
    if (validate_sdt(sdt, "APIC"))
      return (const acpi_madt_t *)sdt;
  }
  return NULL;
}

static void parse_madt(const acpi_madt_t *madt) {
  acpi_info.lapic_phys_addr = madt->lapic_address;
  acpi_info.cpu_count = 0;
  acpi_info.ioapic_phys_addr = 0;

  const uint8_t *p = (const uint8_t *)madt + sizeof(acpi_madt_t);
  const uint8_t *end = (const uint8_t *)madt + madt->hdr.length;

  while (p + 2 <= end) {
    uint8_t type = p[0];
    uint8_t len = p[1];
    if (len < 2 || p + len > end)
      break;

    if (type == ACPI_MADT_TYPE_LAPIC) {
      const acpi_madt_lapic_t *l = (const acpi_madt_lapic_t *)p;
      if (l->flags & ACPI_LAPIC_FLAG_ENABLED) {
        int idx = acpi_info.cpu_count;
        if (idx < (int)(sizeof(acpi_info.cpu_apic_ids) /
                        sizeof(acpi_info.cpu_apic_ids[0]))) {
          acpi_info.cpu_apic_ids[idx] = l->apic_id;
          acpi_info.cpu_count++;
        }
      }
    } else if (type == ACPI_MADT_TYPE_IOAPIC) {
      const acpi_madt_ioapic_t *io = (const acpi_madt_ioapic_t *)p;
      if (acpi_info.ioapic_phys_addr == 0) {
        acpi_info.ioapic_phys_addr = io->ioapic_address;
        acpi_info.ioapic_gsi_base = io->gsi_base;
      }
    } else if (type == ACPI_MADT_TYPE_LAPIC_OVR) {
      uint64_t addr;
      __builtin_memcpy(&addr, p + 4, 8);
      if ((addr >> 32) == 0) {
        acpi_info.lapic_phys_addr = (uint32_t)addr;
      }
    }

    p += len;
  }
}

void acpi_init(void) {
  acpi_info.present = 0;
  acpi_info.cpu_count = 0;

  const acpi_rsdp_t *rsdp = find_rsdp();
  if (!rsdp) {
    kprintf("ACPI: no RSDP found in BIOS area\n");
    return;
  }
  kprintf("ACPI: RSDP @ %x (rev %x)\n", (uint32_t)(uintptr_t)rsdp,
          rsdp->revision);

  const acpi_sdt_header_t *root = NULL;
  int is_xsdt = 0;

  if (rsdp->revision >= 2 && rsdp->xsdt_address &&
      (rsdp->xsdt_address >> 32) == 0) {
    root = validate_sdt(
        (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address, "XSDT");
    if (root)
      is_xsdt = 1;
  }
  if (!root) {
    root = validate_sdt(
        (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address, "RSDT");
  }
  if (!root) {
    kprintf("ACPI: RSDT/XSDT validation failed\n");
    return;
  }

  const acpi_madt_t *madt = find_madt(root, is_xsdt);
  if (!madt) {
    kprintf("ACPI: MADT not found in %s\n", is_xsdt ? "XSDT" : "RSDT");
    return;
  }

  parse_madt(madt);
  acpi_info.present = 1;

  kprintf("ACPI: %d CPU(s), LAPIC @ %x, IOAPIC @ %x (GSI base %x)\n",
          acpi_info.cpu_count, acpi_info.lapic_phys_addr,
          acpi_info.ioapic_phys_addr, acpi_info.ioapic_gsi_base);

  for (int i = 0; i < acpi_info.cpu_count; i++) {
    kprintf("ACPI:   CPU%d -> LAPIC id %x\n", i, acpi_info.cpu_apic_ids[i]);
  }
}
