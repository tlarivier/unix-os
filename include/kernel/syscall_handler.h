#ifndef SYSCALL_HANDLER_H
#define SYSCALL_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#include <../uapi/syscalls.h>

/*
 * Register structure for syscalls - must match syscall_entry.S exactly!
 * 
 * Stack layout after syscall_entry_point pushes (low addr -> high addr):
 *   ds                          <- ESP points here
 *   EDI, ESI, EBP, ESP_dummy, EBX, EDX, ECX, EAX  (PUSHAL order, reversed)
 *   int_no (0x80)
 *   err_code (0)
 *   EIP, CS, EFLAGS, UserESP, SS  (pushed by CPU on INT from Ring 3)
 *
 * PUSHAL pushes: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI (in that order)
 * So in memory from low to high: EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
 */
typedef struct syscall_registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} syscall_registers_t;

void syscall_init(void);
void syscall_handler(syscall_registers_t* regs);

int32_t sys_exit_handler(uint32_t status, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_read_handler(uint32_t fd, uint32_t buffer, size_t count, uint32_t arg4, uint32_t arg5);
int32_t sys_write_handler(uint32_t fd, uint32_t buffer, size_t count, uint32_t arg4, uint32_t arg5);
int32_t sys_open_handler(uint32_t path, uint32_t flags, uint32_t mode, uint32_t unused, uint32_t arg5);
int32_t sys_close_handler(uint32_t fd, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);
int32_t sys_getpid_handler(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4, uint32_t arg5);
int32_t sys_mkdir_handler(uint32_t path, uint32_t mode, uint32_t unused1, uint32_t unused2, uint32_t arg5);
int32_t sys_brk_handler(uint32_t addr, uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t arg5);

extern void syscall_entry_point(void);

#endif 
