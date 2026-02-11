#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/kprintf.h>
#include <stddef.h>

static struct irq_handler irq_handlers[IRQ_COUNT] = {0};

void irq_init(void) {
    for (int i = 0; i < IRQ_COUNT; i++) {
        irq_handlers[i].handler = NULL;
        irq_handlers[i].data    = NULL;
        irq_handlers[i].name    = NULL;
        irq_handlers[i].count   = 0;
    }
}

int irq_register(uint32_t irq, irq_handler_t handler, void *data, const char *name) {
    if (irq >= IRQ_COUNT) return -1;
    if (irq_handlers[irq].handler != NULL) return -2;
    
    irq_handlers[irq].handler = handler;
    irq_handlers[irq].data    = data;
    irq_handlers[irq].name    = name;
    irq_handlers[irq].count   = 0;
    
    irq_enable(irq);
    return 0;
}

int irq_unregister(uint32_t irq) {
    if (irq >= IRQ_COUNT) return -1;
    
    irq_disable(irq);
    irq_handlers[irq].handler = NULL;
    irq_handlers[irq].data    = NULL;
    irq_handlers[irq].name    = NULL;
    return 0;
}

struct irq_handler *irq_get_handler(uint32_t irq) {
    if (irq >= IRQ_COUNT) return NULL;
    return &irq_handlers[irq];
}

void irq_enable(uint32_t irq) {
    uint16_t port;
    uint8_t mask;
    
    if (irq < 8) {
        port = PIC1_DATA;
        mask = inb(port) & ~(1 << irq);
    } else {
        port = PIC2_DATA;
        mask = inb(port) & ~(1 << (irq - 8));
    }
    outb(port, mask);
}

void irq_disable(uint32_t irq) {
    uint16_t port;
    uint8_t mask;
    
    if (irq < 8) {
        port = PIC1_DATA;
        mask = inb(port) | (1 << irq);
    } else {
        port = PIC2_DATA;
        mask = inb(port) | (1 << (irq - 8));
    }
    outb(port, mask);
}

void irq_dispatch(uint32_t irq) {
    if (irq >= IRQ_COUNT) return;
    
    struct irq_handler *h = &irq_handlers[irq];
    if (h->handler) {
        h->count++;
        h->handler(irq, h->data);
    }
}
