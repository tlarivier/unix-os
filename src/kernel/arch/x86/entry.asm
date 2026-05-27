; entry.asm — kernel entry @ 0x100100, common to legacy boot and multiboot1/2.
; Multiboot magic + info pointer arrive in EAX/EBX; we stash them, install our
; own GDT (GRUB's may differ), clear .bss, then hand off to multiboot_parse + kmain.

[BITS 32]

section .entry
extern kmain
extern multiboot_parse
extern _bss_start
extern _bss_end

global _start
_start:
    cli

    mov [multiboot_magic], eax  ; stash BEFORE clobbering eax/ebx
    mov [multiboot_info], ebx

    lgdt [gdt_ptr]
    jmp 0x08:.reload_cs         ; far jump → flush pipeline with new CS

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000

    cld                         ; clear .bss
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    push dword [multiboot_info]
    push dword [multiboot_magic]
    call multiboot_parse
    add esp, 8

    call kmain

halt:
    hlt
    jmp halt

section .data
global multiboot_magic
global multiboot_info
multiboot_magic: dd 0
multiboot_info:  dd 0

; Flat GDT: null / ring0 code (RX) / ring0 data (RW), 4K gran, 32-bit.
align 16
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xFFFF, 0
    db 0, 10010010b, 11001111b, 0
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start
