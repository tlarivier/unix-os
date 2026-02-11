#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/idt.h>

typedef struct registers {
    uint32_t ds;                                    
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;                      
    uint32_t eip, cs, eflags, useresp, ss;          
} registers_t;

extern void isr0(void);   extern void isr1(void);   extern void isr2(void);   extern void isr3(void);
extern void isr4(void);   extern void isr5(void);   extern void isr6(void);   extern void isr7(void);
extern void isr8(void);   extern void isr9(void);   extern void isr10(void);  extern void isr11(void);
extern void isr12(void);  extern void isr13(void);  extern void isr14(void);  extern void isr15(void);
extern void isr16(void);  extern void isr17(void);  extern void isr18(void);  extern void isr19(void);
extern void isr20(void);  extern void isr21(void);  extern void isr22(void);  extern void isr23(void);
extern void isr24(void);  extern void isr25(void);  extern void isr26(void);  extern void isr27(void);
extern void isr28(void);  extern void isr29(void);  extern void isr30(void);  extern void isr31(void);

extern void irq0(void);   extern void irq1(void);   extern void irq2(void);   extern void irq3(void);
extern void irq4(void);   extern void irq5(void);   extern void irq6(void);   extern void irq7(void);
extern void irq8(void);   extern void irq9(void);   extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);  extern void irq14(void);  extern void irq15(void);

void idt_init(void);
void isr_handler(registers_t* regs);
void irq_handler(registers_t* regs);

extern void syscall_entry_point(void);

#define IRQ_TIMER    32   
#define IRQ_KEYBOARD 33   
#define IRQ_CASCADE  2
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_FREE1    9
#define IRQ_FREE2    10
#define IRQ_FREE3    11
#define IRQ_PS2      12
#define IRQ_FPU      13
#define IRQ_ATA1     14
#define IRQ_ATA2     15

#endif 
