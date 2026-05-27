/* gdt_flush(ptr): LGDT + reload ring0 segments (0x08 code, 0x10 data).
 * tss_flush(): LTR with selector 0x28. */

.section .text
.global gdt_flush
.global tss_flush

gdt_flush:
    movl 4(%esp), %eax
    lgdt (%eax)

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    ljmp $0x08, $.gdt_flush_done    /* far jump → reload CS */
.gdt_flush_done:
    ret

tss_flush:
    movw $0x28, %ax
    ltr %ax
    ret
