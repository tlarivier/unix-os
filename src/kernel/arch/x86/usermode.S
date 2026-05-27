/*
 * usermode.S — initial ring 0 → ring 3 transition.
 *   void jump_to_usermode(uint32_t entry_eip, uint32_t user_esp);
 * Builds an IRET frame (SS, ESP, EFLAGS, CS, EIP) with user selectors
 * (CS=0x1B, SS=0x23 — both ring-3), EFLAGS=0x202 (IF=1, reserved bit).
 * Clears GP regs before IRET to avoid leaking kernel state.
 */

.section .text
.global jump_to_usermode

jump_to_usermode:
    cli

    movl 4(%esp), %eax          /* entry_eip */
    movl 8(%esp), %ebx          /* user_esp  (capture BEFORE we touch segs) */

    pushl $0x23                 /* SS = user data | RPL 3 */
    pushl %ebx                  /* ESP */
    pushl $0x202                /* EFLAGS: IF=1, bit1 reserved=1 */
    pushl $0x1B                 /* CS = user code | RPL 3 */
    pushl %eax                  /* EIP */

    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    xorl %eax, %eax             /* scrub GP regs */
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp

    iret
