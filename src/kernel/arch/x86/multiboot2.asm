; multiboot2.asm — Multiboot1 + Multiboot2 headers.
; MB1 is needed for QEMU `-kernel`; MB2 for GRUB2 (we request module-alignment
; and an info-tag with MODULES + MMAP so initramfs gets loaded as a module).
; Entry symbol lives in entry.asm at 0x100100.

[BITS 32]

MULTIBOOT1_MAGIC        equ 0x1BADB002
MULTIBOOT1_FLAGS        equ 0x00000003
MULTIBOOT1_CHECKSUM     equ -(MULTIBOOT1_MAGIC + MULTIBOOT1_FLAGS)

section .multiboot1
align 4
multiboot1_header:
    dd MULTIBOOT1_MAGIC
    dd MULTIBOOT1_FLAGS
    dd MULTIBOOT1_CHECKSUM

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

    align 8                     ; module alignment request
    dw MB2_TAG_MODULE_ALIGN
    dw 0
    dd 8

    align 8                     ; ask GRUB for modules + memory map info
    dw MB2_TAG_INFO_REQUEST
    dw 0
    dd 16
    dd MB2_INFO_MODULES
    dd MB2_INFO_MMAP

    align 8                     ; end tag
    dw MB2_TAG_END
    dw 0
    dd 8
multiboot2_header_end:
