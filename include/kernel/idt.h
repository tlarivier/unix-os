#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

typedef struct idt_entry {
  uint16_t base_low;
  uint16_t selector;
  uint8_t zero;
  uint8_t flags;
  uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) idt_ptr_t;

#define IDT_FLAG_INT_GATE_DPL0 0x8E
#define IDT_FLAG_INT_GATE_DPL3 0xEE

void idt_init(void);
void idt_init_ap(void);

extern void idt_flush(uint32_t idt_ptr);

extern void irq239(void);
extern void irq251(void);
extern void irq252(void);
extern void irq253(void);

#endif
