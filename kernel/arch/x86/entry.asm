[BITS 32]

; Kernel entry section - at fixed address 0x100100
section .entry
extern kmain
extern multiboot_parse

; Import linker symbols for .bss section
extern _bss_start
extern _bss_end

; Unified kernel entry point for both legacy and multiboot boot
global _start
_start:
    ; Disable interrupts
    cli
    
    ; Save multiboot info (before we touch any registers)
    mov [multiboot_magic], eax
    mov [multiboot_info], ebx
    
    ; Load our own GDT (GRUB's GDT may differ from legacy bootloader's)
    lgdt [gdt_ptr]
    
    ; Far jump to reload CS
    jmp 0x08:.reload_cs

.reload_cs:
    ; Setup data segments with our GDT
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Setup kernel stack
    mov esp, 0x90000
    
    ; Clear .bss section to zero
    cld
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    
    ; Parse multiboot info if booted via multiboot
    push dword [multiboot_info]
    push dword [multiboot_magic]
    call multiboot_parse
    add esp, 8
    
    ; Call the main kernel function
    call kmain
    
    ; If kmain returns, halt
halt:
    hlt
    jmp halt

; Multiboot info storage (in .data section)
section .data
global multiboot_magic
global multiboot_info
multiboot_magic: dd 0
multiboot_info:  dd 0

; GDT for kernel (flat memory model)
align 16
gdt_start:
    dq 0                        ; Null descriptor
gdt_code:
    dw 0xFFFF                   ; Limit low
    dw 0                        ; Base low
    db 0                        ; Base mid
    db 10011010b                ; Access: Present, Ring 0, Code, Readable
    db 11001111b                ; Flags: 4KB gran, 32-bit + Limit high
    db 0                        ; Base high
gdt_data:
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b                ; Access: Present, Ring 0, Data, Writable
    db 11001111b
    db 0
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1  ; Limit
    dd gdt_start                ; Base
