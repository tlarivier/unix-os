#ifndef KERNEL_ERROR_HANDLER_H
#define KERNEL_ERROR_HANDLER_H

/* Heavy variant — pulls in interrupt.h for `registers_t`. For the panic
 * macro alone, prefer <kernel/panic.h>. */
#include <kernel/interrupt.h>
#include <kernel/panic.h>

void error_dump_and_halt(registers_t *regs);

#endif
