#ifndef KERNEL_ARCH_X86_ACPI_TABLES_H
#define KERNEL_ARCH_X86_ACPI_TABLES_H

#include <stdint.h>

#define ACPI_MADT_TYPE_LAPIC 0
#define ACPI_MADT_TYPE_IOAPIC 1
#define ACPI_MADT_TYPE_LAPIC_OVR 5

#define ACPI_LAPIC_FLAG_ENABLED 0x1

typedef struct __attribute__((packed)) {
  char signature[4]; /* e.g. "APIC", "FACP", "DSDT" */
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
  acpi_sdt_header_t hdr;
  uint32_t lapic_address;
  uint32_t flags;
} acpi_madt_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint8_t acpi_processor_id;
  uint8_t apic_id;
  uint32_t flags;
} acpi_madt_lapic_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t length;
  uint8_t ioapic_id;
  uint8_t reserved;
  uint32_t ioapic_address;
  uint32_t gsi_base;
} acpi_madt_ioapic_t;

typedef struct __attribute__((packed)) {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} acpi_rsdp_t;

#endif
