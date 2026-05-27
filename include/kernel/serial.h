#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_handle_irq(void);

#endif
