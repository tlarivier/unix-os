;
; boot.asm - x86 Bootloader
;

[org 0x7c00]
[bits 16]

; ============================================================================
; Constants
; ============================================================================
KERNEL_OFFSET   equ 0x10000     ; legacy bootloader address
KERNEL_SEGMENTS equ 320         ; ~160KB, covers full kernel with embedded binaries

; ============================================================================
; Entry Point
; ============================================================================
start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    
    mov [boot_drive], dl        ; Save boot drive from BIOS
    
    mov si, msg_boot
    call print
    
    call load_kernel
    call enter_protected_mode
    
    jmp $                       ; Should never reach

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

; ============================================================================
; Load kernel from disk LBA-style 
; ============================================================================
load_kernel:
    mov si, msg_loading
    call print
    
    mov ax, 0x1000              ; ES:BX = 0x1000:0000 = 0x10000
    mov es, ax
    xor bx, bx
    
    mov cl, 2                   ; Start at sector 2 (after boot)
    mov ch, 0                   ; Cylinder 0
    mov dh, 0                   ; Head 0
    mov di, KERNEL_SEGMENTS     ; Sectors to read
    
.read_loop:
    cmp di, 0
    je .done
    
    ; Reset disk before each read 
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    
    ; Read 1 sector at a time 
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]
    int 0x13
    jc .error
    
    ; Advance buffer by 512 bytes
    add bx, 512
    jnc .no_segment_overflow
    
    ; Handle segment overflow (every 64KB)
    mov ax, es
    add ax, 0x1000
    mov es, ax
    xor bx, bx
    
.no_segment_overflow:
    ; Advance CHS address
    inc cl                      ; Next sector
    cmp cl, 19                  ; Sectors 1-18 per track
    jne .next_sector
    
    mov cl, 1                   ; Reset to sector 1
    inc dh                      ; Next head
    cmp dh, 2                   ; 2 heads (0-1)
    jne .next_sector
    
    xor dh, dh                  ; Reset head
    inc ch                      ; Next cylinder
    
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

; ============================================================================
; Enter 32-bit Protected Mode
; ============================================================================
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

; ============================================================================
; GDT 
; ============================================================================
gdt_start:
    dq 0                        ; Null descriptor

gdt_code:
    dw 0xffff                   ; Limit
    dw 0                        ; Base (low)
    db 0                        ; Base (mid)
    db 10011010b                ; Access: Present, Ring 0, Code, Readable
    db 11001111b                ; Flags: 4KB granularity, 32-bit
    db 0                        ; Base (high)

gdt_data:
    dw 0xffff
    dw 0
    db 0
    db 10010010b                ; Access: Present, Ring 0, Data, Writable
    db 11001111b
    db 0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ============================================================================
; 32-bit Protected Mode
; ============================================================================
[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    
    ; Copy kernel from 0x10000 to 0x100000 (1MB)
    cld
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, KERNEL_SEGMENTS * 128  ; dwords
    rep movsd
    
    ; Jump to kernel entry (0x100100)
    jmp 0x08:0x100100

; ============================================================================
; Data
; ============================================================================
[bits 16]
boot_drive:     db 0
msg_boot:       db 'Unix-OS Bootloader', 13, 10, 0
msg_loading:    db 'Loading kernel...', 0
msg_ok:         db 'OK', 13, 10, 0
msg_error:      db 'DISK ERROR', 13, 10, 0

; ============================================================================
; Boot signature
; ============================================================================
times 510-($-$$) db 0
dw 0xaa55
