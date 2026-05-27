/*
 * ap_trampoline.S — AP startup blob, copied to phys 0x8000.
 *
 * BSP sends STARTUP IPI vector 0x08 → AP boots in real mode at 0x8000.
 * Linked at 0x8000 so label refs are physical; objcopy'd to raw binary
 * and embedded in the kernel image.
 *
 * BSP fills a "rendezvous" struct at phys 0x9000 before each SIPI burst
 * (serial — one AP at a time, so no per-id array needed):
 *   0x9000  cr3            kernel page directory (phys)
 *   0x9004  stack_top      per-AP top-of-stack
 *   0x9008  ap_main_addr   virtual addr of ap_main()
 *   0x900C  my_cpu_id      logical cpu id for THIS AP
 */

.code16
.section .text
.global ap_trampoline_start
.global ap_trampoline_end

ap_trampoline_start:
    cli
    cld
    lgdtl   ap_gdt_ptr

    movl    %cr0, %eax          /* CR0.PE = 1 (paging still off) */
    orl     $1, %eax
    movl    %eax, %cr0

    ljmpl   $0x08, $ap_pm32     /* flush pipeline with 32-bit CS */

.code32
ap_pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    movl    0x9000, %eax        /* CR3 ← kernel page directory */
    movl    %eax, %cr3

    movl    %cr0, %eax          /* CR0.PG = 1 — paging on (identity map keeps low mem valid) */
    orl     $0x80000000, %eax
    movl    %eax, %cr0

    movl    0x9004, %esp
    movl    0x900C, %eax        /* arg0 = my_cpu_id */
    pushl   %eax
    movl    0x9008, %eax
    call    *%eax               /* ap_main() — should never return */

ap_halt:
    cli
    hlt
    jmp     ap_halt

/* GDT: null / ring0 code RX (0x08) / ring0 data RW (0x10), 4K gran, 32-bit. */
.align 8
ap_gdt:
    .quad   0
    .quad   0x00CF9A000000FFFF
    .quad   0x00CF92000000FFFF
ap_gdt_end:

ap_gdt_ptr:
    .word   ap_gdt_end - ap_gdt - 1
    .long   ap_gdt              /* phys == link addr */

ap_trampoline_end:
