#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

typedef struct idt_entry {
    uint16_t base_low;      
    uint16_t selector;      
    uint8_t  zero;          
    uint8_t  flags;         
    uint16_t base_high;     
} __attribute__((packed)) idt_entry_t;

typedef struct idt_ptr {
    uint16_t limit;         
    uint32_t base;          
} __attribute__((packed)) idt_ptr_t;

void idt_init(void);
void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags);

extern void idt_flush(uint32_t idt_ptr);

#endif 
