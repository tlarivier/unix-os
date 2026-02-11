.section .text
.global _start
.type _start, @function

_start:
    mov %esp, %eax      /* Save original esp */
    push %eax           /* Pass as argument to _start_c */
    call _start_c       /* Direct call - not PIC */
    add $4, %esp
    /* Should not return, but just in case */
    mov $1, %eax        /* exit syscall */
    xor %ebx, %ebx
    int $0x80
