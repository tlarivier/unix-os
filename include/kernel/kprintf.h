#ifndef KERNEL_KPRINTF_H
#define KERNEL_KPRINTF_H

#include <stdint.h>

void kprintf(const char *format, ...);
void kprintf_enable_locking(void);
void kprintf_panic(const char *msg);

#endif
