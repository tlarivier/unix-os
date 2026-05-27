/*
 * serial.c — 16550 UART (COM1, port 0x3F8) driver: controller init, IRQ-driven
 * RX polling that pushes into the TTY ringbuf via kb_external_push, and
 * synchronous busy-wait TX used by kprintf mirroring and syscall writes.
 *
 * Invariants:
 *  - All port reads/writes use inb/outb from <kernel/io.h>; COM1_PORT comes
 *    from <kernel/ports.h>.
 *  - serial_handle_irq runs in IRQ context; only ERBFI is enabled in IER so
 *    the handler only needs to drain RX while serial_data_ready().
 *  - RX bytes are normalized CR -> LF before being pushed into the TTY ringbuf
 *    (line-discipline parity with PS/2).
 *  - serial_putc busy-waits on THRE; never blocks the scheduler.
 *
 * Not allowed:
 *  - Calling kmalloc, schedule, wait_*, vfs_* from this TU (IRQ context).
 *  - Touching state owned by keyboard.c outside the kb_external_push API.
 *  - Defining local outb/inb inlines or magic COM1 constants.
 */

#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/ports.h>
#include <kernel/serial.h>
#include <stdint.h>

void serial_init(void) {
  outb(COM1_PORT + 1, 0x00); /* Disable interrupts */
  outb(COM1_PORT + 3, 0x80); /* Enable DLAB */
  outb(COM1_PORT + 0, 0x03); /* Baud rate 38400 (lo) */
  outb(COM1_PORT + 1, 0x00); /* Baud rate (hi) */
  outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, 1 stop */
  outb(COM1_PORT + 2, 0xC7); /* FIFO */
  outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set, OUT2 (IRQ line) on */
  outb(COM1_PORT + 1, 0x01); /* Enable Received Data Available Interrupt */
  irq_register(IRQ_COM1_VEC, serial_handle_irq);
}

static int serial_data_ready(void) { return inb(COM1_PORT + 5) & 0x01; }

void serial_handle_irq(void) {
  while (serial_data_ready()) {
    uint8_t b = inb(COM1_PORT);
    if (b == '\r')
      b = '\n';
    kb_external_push((char)b);
  }
}

static int serial_is_transmit_empty(void) { return inb(COM1_PORT + 5) & 0x20; }

void serial_putc(char c) {
  while (!serial_is_transmit_empty())
    ;
  outb(COM1_PORT, c);
}
