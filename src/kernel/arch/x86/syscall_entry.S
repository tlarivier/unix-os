/*
 * syscall_entry.S — INT 0x80 entry.
 * CPU pushed (from ring 3): SS, ESP, EFLAGS, CS, EIP.
 * We pad with (err_code=0, int_no=0x80) so the frame matches syscall_registers_t,
 * push gs as a scratch slot (not in the struct), and load per-CPU gs computed
 * from LAPIC ID at 0xFEE00020 (selector = 0x68 + lapic_id*8) so this_cpu()
 * works on APs too.
 */

.section .text
.global syscall_entry_point

syscall_entry_point:
    pushl $0                    /* err_code */
    pushl $0x80                 /* int_no */

    pushal
    pushl %ds                   /* ds → into struct */
    pushl %gs                   /* extra slot, restored on exit */

    movw $0x10, %ax             /* kernel data */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs

    /* per-CPU gs: read LAPIC ID MMIO, selector = 0x68 + lapic_id*8.
     * Load MMIO addr via register to dodge GAS abs-operand ambiguity. */
    movl $0xFEE00020, %edx
    movl (%edx), %eax
    shrl $24, %eax
    shll $3, %eax
    addl $0x68, %eax
    movw %ax, %gs

    leal 4(%esp), %eax          /* &regs (skip saved gs above esp) */
    pushl %eax
    call syscall_handler
    addl $4, %esp

    popl %gs
    popl %ds
    popal
    addl $8, %esp               /* drop int_no, err_code */
    iret
