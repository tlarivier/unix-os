.section .text
.global fork_child_return

/*
 * fork_child_return - Return path for forked child processes
 *
 * When a forked child is first scheduled, asm_context_switch jumps here.
 * ESP points to the copied syscall frame on the child's kernel stack.
 *
 * Stack layout (syscall_registers_t):
 *   ESP+0:  ds
 *   ESP+4:  edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax (pushal)
 *   ESP+36: int_no, err_code
 *   ESP+44: eip, cs, eflags, useresp, ss (iret frame)
 */
fork_child_return:
    popl %eax               /* Pop saved ds (0x23) */
    popal                   /* Restore general registers */
    addl $8, %esp           /* Skip int_no and err_code */
    
    /* Load user data segments */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    xorl %eax, %eax         /* fork() returns 0 in child */
    iret
