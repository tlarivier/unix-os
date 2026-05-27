#ifndef KERNEL_ARCH_X86_GDT_INTERNAL_H
#define KERNEL_ARCH_X86_GDT_INTERNAL_H

#include <stdint.h>

typedef struct gdt_entry {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_middle;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct tss_entry {
  uint32_t prev_tss;
  uint32_t esp0;
  uint32_t ss0;
  uint32_t esp1;
  uint32_t ss1;
  uint32_t esp2;
  uint32_t ss2;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
  uint32_t es, cs, ss, ds, fs, gs;
  uint32_t ldt;
  uint16_t trap;
  uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

extern void gdt_flush(uint32_t gdt_ptr);

#endif
