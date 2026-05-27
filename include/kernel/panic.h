#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <kernel/kprintf.h>
#include <stdint.h>

void kernel_panic(const char *message, const char *file, uint32_t line);

#define KERNEL_ERROR(code, msg)                                                \
  kprintf("ERROR [%d]: %s (%s:%u)\n", (int)(code), (msg), __FILE__, __LINE__)
#define KERNEL_PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)

#endif
