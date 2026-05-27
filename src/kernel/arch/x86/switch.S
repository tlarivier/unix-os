/*
 * switch.S — kernel-to-kernel context switch (no segment regs saved).
 *
 *   void asm_context_switch(process_context_t *old, process_context_t *new)
 *
 * process_context_t (40B):
 *    0  eax     16  esi    32  eip
 *    4  ebx     20  edi    36  eflags
 *    8  ecx     24  ebp
 *   12  edx     28  esp
 *
 * old == NULL → load only.  new == NULL → no-op return.
 * On resume, the saved task observes eax == 0.
 */

.section .text
.global asm_context_switch

asm_context_switch:
    movl 4(%esp), %eax          /* old */
    movl 8(%esp), %edx          /* new */

    testl %eax, %eax
    jz .load_new
    testl %edx, %edx
    jz .done

    /* --- save into *old --- */
    movl %ebx,  4(%eax)
    movl %ecx,  8(%eax)
    movl %esi, 16(%eax)
    movl %edi, 20(%eax)
    movl %ebp, 24(%eax)

    movl %esp, %ecx             /* esp at resume = caller's esp (skip ret addr) */
    addl $4, %ecx
    movl %ecx, 28(%eax)

    movl (%esp), %ecx           /* eip = our return address */
    movl %ecx, 32(%eax)

    pushfl                      /* eflags */
    popl %ecx
    movl %ecx, 36(%eax)

    movl $0, 0(%eax)            /* eax on resume = 0 */

.load_new:
    testl %edx, %edx
    jz .done

    /* --- restore from *new --- */
    movl 36(%edx), %eax         /* eflags first */
    pushl %eax
    popfl

    movl  4(%edx), %ebx
    movl  8(%edx), %ecx
    movl 16(%edx), %esi
    movl 20(%edx), %edi
    movl 24(%edx), %ebp

    movl 28(%edx), %esp
    movl 32(%edx), %eax         /* target eip */

    /* edx is still our pointer; restore it last via the new stack. */
    pushl %eax
    movl 12(%edx), %edx
    movl 0(%esp), %eax
    addl $4, %esp

    jmp *%eax

.done:
    ret
