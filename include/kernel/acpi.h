#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

typedef struct {
  int present;
  int cpu_count;
  uint8_t cpu_apic_ids[8];
  uint32_t lapic_phys_addr;
  uint32_t ioapic_phys_addr;
  uint32_t ioapic_gsi_base;
} acpi_info_t;

extern acpi_info_t acpi_info;

void acpi_init(void);

#endif
