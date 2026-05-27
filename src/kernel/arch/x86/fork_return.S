/*
 * fork_return.S — first-scheduled return path for a forked child.
 * asm_context_switch jumps here with ESP pointing at the child's copied
 * syscall frame (syscall_registers_t):
 *    +0   ds
 *    +4   edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax     (pushal)
 *   +36   int_no, err_code
 *   +44   eip, cs, eflags, useresp, ss                     (iret frame)
 * Child observes fork() == 0.
 */

.section .text
.global fork_child_return

fork_child_return:
    popl %eax               /* saved ds */
    popal
    addl $8, %esp           /* skip int_no, err_code */

    movw $0x23, %ax         /* user data | RPL 3 */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    xorl %eax, %eax         /* fork() → 0 in child */
    iret
