#include <kernel/idt.h>
#include <kernel/interrupt.h>
#include <kernel/kernel.h>
#include <kernel/gdt.h>
#include <kernel/io.h>
#include <kernel/ports.h>

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

void idt_set_gate(int num, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[num].base_low = handler & 0xFFFF;
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
    idt[num].base_high = (handler >> 16) & 0xFFFF;
}

void pic_remap(void) {
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, mask1);
    outb(0xA1, mask2);
}

void pic_enable_irq(int irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t value = inb(port) & ~(1 << (irq % 8));
    outb(port, value);
}

void idt_init(void) {
    pic_remap();
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E); /* Divide by zero */
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E); /* Debug */
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E); /* NMI */
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E); /* Breakpoint */
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E); /* Overflow */
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E); /* Bound range */
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E); /* Invalid opcode */
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E); /* Device not available */
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E); /* Double fault */
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E); /* Coprocessor segment */
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E); /* Invalid TSS */
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E); /* Segment not present */
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E); /* Stack-segment fault */
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E); /* GPF */
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); /* Page fault */
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E); /* Reserved */
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E); /* x87 FPU error */
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E); /* Alignment check */
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E); /* Machine check */
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E); /* SIMD FP */
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E); /* Virtualization */
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E); /* Control protection */
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E); /* Reserved */
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E); /* Reserved */
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E); /* Reserved */
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E); /* Reserved */
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E); /* Reserved */
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E); /* Reserved */
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E); /* Hypervisor */
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E); /* VMM */
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E); /* Security */
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E); /* Reserved */
    
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  /* Timer */
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);  /* Keyboard */
    
    idt_set_gate(0x80, (uint32_t)syscall_entry_point, 0x08, 0xEE);
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;
    idt_flush((uint32_t)&idt_ptr);
    pic_enable_irq(0);
    pic_enable_irq(1);
}
