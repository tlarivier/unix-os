/* set_cr3(pd_phys): CR3 ← arg. */

.section .text
.global set_cr3

set_cr3:
    movl 4(%esp), %eax
    movl %eax, %cr3
    ret
