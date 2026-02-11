[BITS 32]

extern syscall_handler

global syscall_int_handler

; System call interrupt handler (INT 0x80)
syscall_int_handler:
    ; Save all registers
    pusha
    mov ax, ds
    push eax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call C handler with pointer to register structure
    push esp
    call syscall_handler
    add esp, 4
    
    ; Result is in EAX, update the saved EAX on stack
    ; The saved EAX is at offset 28 from current ESP (after pusha and segment save)
    mov [esp + 28], eax
    
    ; Restore segments and registers
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa
    iret
