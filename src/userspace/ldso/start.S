/* ldso entry: pass original esp to _start_c; fall back to exit(1) if it returns. */

.section .text
.global _start
.type _start, @function

_start:
    mov %esp, %eax
    push %eax
    call _start_c
    add $4, %esp

    mov $1, %eax                /* exit syscall */
    xor %ebx, %ebx
    int $0x80
