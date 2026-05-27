#ifndef SYSCALL_HANDLER_H
#define SYSCALL_HANDLER_H

#include <stdint.h>

typedef struct syscall_registers {
  uint32_t ds;
  uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
  uint32_t int_no, err_code;
  uint32_t eip, cs, eflags, useresp, ss;
} syscall_registers_t;

void syscall_handler(syscall_registers_t *regs);

extern void syscall_entry_point(void);

#endif
