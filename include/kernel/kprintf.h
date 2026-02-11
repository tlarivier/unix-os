#ifndef KERNEL_KPRINTF_H
#define KERNEL_KPRINTF_H

#include <stdint.h>

void kprintf(const char* format, ...);
void itoa(uint32_t num, char* buffer, int base);

#endif 
