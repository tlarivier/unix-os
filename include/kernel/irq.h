#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

#include <stdint.h>

#define IRQ0  0   /* Timer */
#define IRQ1  1   /* Keyboard */
#define IRQ2  2   /* Cascade */
#define IRQ3  3   /* COM2 */
#define IRQ4  4   /* COM1 */
#define IRQ5  5   /* LPT2 */
#define IRQ6  6   /* Floppy */
#define IRQ7  7   /* LPT1 */
#define IRQ8  8   /* RTC */
#define IRQ9  9   /* Free */
#define IRQ10 10  /* Free */
#define IRQ11 11  /* Free */
#define IRQ12 12  /* PS/2 Mouse */
#define IRQ13 13  /* FPU */
#define IRQ14 14  /* Primary ATA */
#define IRQ15 15  /* Secondary ATA */

#define IRQ_COUNT 16

typedef void (*irq_handler_t)(uint32_t irq, void *data);

struct irq_handler {
    irq_handler_t handler;
    void *data;
    const char *name;
    uint32_t count;  /* Interrupt count for debugging */
};

int irq_register(uint32_t irq, irq_handler_t handler, void *data, const char *name);
int irq_unregister(uint32_t irq);
struct irq_handler *irq_get_handler(uint32_t irq);
void irq_enable(uint32_t irq);
void irq_disable(uint32_t irq);
void irq_init(void);
void irq_dispatch(uint32_t irq);

#endif 
