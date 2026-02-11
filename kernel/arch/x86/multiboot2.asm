;
; multiboot.asm - Multiboot1 & Multiboot2 headers
; 
; Supports both:
; - Multiboot1: QEMU -kernel option
; - Multiboot2: GRUB2 with external initramfs module
;
; Entry point is in entry.asm at fixed address 0x100100
;

[BITS 32]

; ============================================================================
; Multiboot1 Header (for QEMU -kernel)
; ============================================================================
MULTIBOOT1_MAGIC        equ 0x1BADB002
MULTIBOOT1_FLAGS        equ 0x00000003
MULTIBOOT1_CHECKSUM     equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

section .multiboot1
align 4

multiboot1_header:
    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS
    dd MULTIBOOT1_CHECKSUM

; ============================================================================
; Multiboot2 Header (for GRUB2)
; ============================================================================
MULTIBOOT2_MAGIC        equ 0xe85250d6
MULTIBOOT2_ARCH_I386    equ 0
MULTIBOOT2_HEADER_LEN   equ (multiboot2_header_end - multiboot2_header)
MULTIBOOT2_CHECKSUM     equ -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_I386 + MULTIBOOT2_HEADER_LEN)

MB2_TAG_END             equ 0
MB2_TAG_INFO_REQUEST    equ 1
MB2_TAG_MODULE_ALIGN    equ 6
MB2_INFO_MODULES        equ 3
MB2_INFO_MMAP           equ 6

section .multiboot
align 8

multiboot2_header:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH_I386
    dd MULTIBOOT2_HEADER_LEN
    dd MULTIBOOT2_CHECKSUM
    
    ; Request module alignment
    align 8
    dw MB2_TAG_MODULE_ALIGN
    dw 0
    dd 8
    
    ; Request modules and memory map info
    align 8
    dw MB2_TAG_INFO_REQUEST
    dw 0
    dd 16
    dd MB2_INFO_MODULES
    dd MB2_INFO_MMAP
    
    ; End tag
    align 8
    dw MB2_TAG_END
    dw 0
    dd 8
multiboot2_header_end:
