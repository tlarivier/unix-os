/*
 * idt.c — Build and install the 256-entry IDT on the BSP (32 exceptions,
 * 3 IOAPIC-routed IRQs, 4 LAPIC/IPI vectors, 1 syscall gate at DPL=3) and
 * reload IDTR on each AP via idt_init_ap().
 *
 * Invariants:
 *  - The idt[256] table is shared by all CPUs and read-only after
 *    idt_init(); only the IDTR register is per-CPU.
 *  - Exception/IRQ/IPI gates use DPL=0 (0x8E); the syscall gate at 0x80
 *    is the only DPL=3 entry (0xEE).
 *  - pic_remap() runs before IOAPIC takeover so a stray 8259 spurious IRQ
 *    cannot masquerade as a CPU exception; PIC lines stay masked.
 *  - All vector targets (isrN, irqN, irq239/251/252/253, syscall_entry_point)
 *    are external asm stubs; no C dispatch lives here.
 *
 * Not allowed:
 *  - Runtime dispatch logic (belongs in interrupt.c).
 *  - Direct EOI / handler invocation (belongs in interrupt.c).
 *  - Mutating idt[] after sti on the BSP or after idt_init_ap() on an AP.
 */

#include <kernel/constants.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/syscall_handler.h>

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

static void idt_set_gate(int num, uint32_t handler, uint16_t sel,
                         uint8_t flags) {
  idt[num].base_low = handler & 0xFFFF;
  idt[num].selector = sel;
  idt[num].zero = 0;
  idt[num].flags = flags;
  idt[num].base_high = (handler >> 16) & 0xFFFF;
}

static void pic_remap(void) {
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

void idt_init(void) {
  pic_remap();
  idt_set_gate(0, (uint32_t)isr0, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Divide by zero */
  idt_set_gate(1, (uint32_t)isr1, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Debug */
  idt_set_gate(2, (uint32_t)isr2, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* NMI */
  idt_set_gate(3, (uint32_t)isr3, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Breakpoint */
  idt_set_gate(4, (uint32_t)isr4, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Overflow */
  idt_set_gate(5, (uint32_t)isr5, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Bound range */
  idt_set_gate(6, (uint32_t)isr6, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Invalid opcode */
  idt_set_gate(7, (uint32_t)isr7, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Device not available */
  idt_set_gate(8, (uint32_t)isr8, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Double fault */
  idt_set_gate(9, (uint32_t)isr9, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Coprocessor segment */
  idt_set_gate(10, (uint32_t)isr10, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Invalid TSS */
  idt_set_gate(11, (uint32_t)isr11, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Segment not present */
  idt_set_gate(12, (uint32_t)isr12, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Stack-segment fault */
  idt_set_gate(13, (uint32_t)isr13, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* GPF */
  idt_set_gate(14, (uint32_t)isr14, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Page fault */
  idt_set_gate(15, (uint32_t)isr15, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(16, (uint32_t)isr16, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* x87 FPU error */
  idt_set_gate(17, (uint32_t)isr17, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Alignment check */
  idt_set_gate(18, (uint32_t)isr18, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Machine check */
  idt_set_gate(19, (uint32_t)isr19, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* SIMD FP */
  idt_set_gate(20, (uint32_t)isr20, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Virtualization */
  idt_set_gate(21, (uint32_t)isr21, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Control protection */
  idt_set_gate(22, (uint32_t)isr22, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(23, (uint32_t)isr23, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(24, (uint32_t)isr24, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(25, (uint32_t)isr25, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(26, (uint32_t)isr26, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(27, (uint32_t)isr27, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */
  idt_set_gate(28, (uint32_t)isr28, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Hypervisor */
  idt_set_gate(29, (uint32_t)isr29, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* VMM */
  idt_set_gate(30, (uint32_t)isr30, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Security */
  idt_set_gate(31, (uint32_t)isr31, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Reserved */

  idt_set_gate(32, (uint32_t)irq0, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Timer */
  idt_set_gate(33, (uint32_t)irq1, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* Keyboard */
  idt_set_gate(36, (uint32_t)irq4, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* COM1 serial */

  idt_set_gate(0xFB, (uint32_t)irq251, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* CALL_FUNCTION */
  idt_set_gate(0xFC, (uint32_t)irq252, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* RESCHED       */
  idt_set_gate(0xFD, (uint32_t)irq253, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* TLB_FLUSH     */
  idt_set_gate(0xEF, (uint32_t)irq239, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL0); /* LAPIC timer   */

  idt_set_gate(0x80, (uint32_t)syscall_entry_point, KERNEL_CODE_SEL,
               IDT_FLAG_INT_GATE_DPL3);

  idt_ptr.limit = sizeof(idt) - 1;
  idt_ptr.base = (uint32_t)&idt;
  idt_flush((uint32_t)&idt_ptr);
}

void idt_init_ap(void) { idt_flush((uint32_t)&idt_ptr); }
