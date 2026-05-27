; boot.asm — legacy 16-bit floppy bootloader for build/bin/os.img (`make test`/`debug`).
; NOT on the default boot path: QEMU `-kernel` and GRUB use multiboot via
; entry.asm + multiboot2.asm. Kept to preserve the floppy-boot test target
; (see docs/ARCHITECTURE.md) — don't regress without updating the test plan.

[org 0x7c00]
[bits 16]

KERNEL_OFFSET   equ 0x10000     ; legacy load address
KERNEL_SEGMENTS equ 320         ; ~160KB, full kernel + embedded binaries

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    mov [boot_drive], dl        ; BIOS passes boot drive in DL

    mov si, msg_boot
    call print

    call load_kernel
    call enter_protected_mode

    jmp $                       ; unreachable

print:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Read KERNEL_SEGMENTS sectors via BIOS int 13h into ES:BX = 0x1000:0000.
; Walks CHS manually (18 sectors/track, 2 heads); handles 64K segment rollover.
load_kernel:
    mov si, msg_loading
    call print

    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov cl, 2                   ; start at sector 2 (sector 1 = this boot code)
    mov ch, 0
    mov dh, 0
    mov di, KERNEL_SEGMENTS

.read_loop:
    cmp di, 0
    je .done

    xor ax, ax                  ; reset disk before each read
    mov dl, [boot_drive]
    int 0x13

    mov ah, 0x02                ; read 1 sector
    mov al, 1
    mov dl, [boot_drive]
    int 0x13
    jc .error

    add bx, 512
    jnc .no_segment_overflow

    mov ax, es                  ; bx wrapped — bump ES by 0x1000 (64K)
    add ax, 0x1000
    mov es, ax
    xor bx, bx

.no_segment_overflow:
    inc cl
    cmp cl, 19                  ; sectors 1..18 per track
    jne .next_sector

    mov cl, 1
    inc dh
    cmp dh, 2                   ; 2 heads
    jne .next_sector

    xor dh, dh
    inc ch                      ; next cylinder

.next_sector:
    dec di
    jmp .read_loop

.done:
    mov si, msg_ok
    call print
    ret

.error:
    mov si, msg_error
    call print
    jmp $

; A20 via fast gate (port 0x92), load GDT, set CR0.PE, far jump to flush pipeline.
enter_protected_mode:
    cli

    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_mode

; Flat GDT: null / ring0 code (RX) / ring0 data (RW), 4K gran, 32-bit.
gdt_start:
    dq 0
gdt_code:
    dw 0xffff, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xffff, 0
    db 0, 10010010b, 11001111b, 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Relocate kernel 0x10000 → 0x100000 (1MB), then jump to entry at 0x100100.
    cld
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, KERNEL_SEGMENTS * 128  ; dwords
    rep movsd

    jmp 0x08:0x100100

[bits 16]
boot_drive:     db 0
msg_boot:       db 'Unix-OS Bootloader', 13, 10, 0
msg_loading:    db 'Loading kernel...', 0
msg_ok:         db 'OK', 13, 10, 0
msg_error:      db 'DISK ERROR', 13, 10, 0

times 510-($-$$) db 0
dw 0xaa55
