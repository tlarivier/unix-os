[BITS 32]

extern isr_handler
extern irq_handler

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0          ; Dummy error code (CPU doesn't push one)
    push %1         ; ISR number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    ; CPU already pushed error code
    push %1         ; ISR number
    jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common_stub
%endmacro

; Define all CPU exception ISRs (0-31)
; Exceptions WITHOUT error code pushed by CPU
ISR_NOERRCODE 0     ; Divide by zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound range exceeded
ISR_NOERRCODE 6     ; Invalid opcode
ISR_NOERRCODE 7     ; Device not available
ISR_ERRCODE   8     ; Double fault (HAS error code)
ISR_NOERRCODE 9     ; Coprocessor segment overrun
ISR_ERRCODE   10    ; Invalid TSS (HAS error code)
ISR_ERRCODE   11    ; Segment not present (HAS error code)
ISR_ERRCODE   12    ; Stack-segment fault (HAS error code)
ISR_ERRCODE   13    ; General protection fault (HAS error code)
ISR_ERRCODE   14    ; Page fault (HAS error code)
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU error
ISR_ERRCODE   17    ; Alignment check (HAS error code)
ISR_NOERRCODE 18    ; Machine check
ISR_NOERRCODE 19    ; SIMD floating-point
ISR_NOERRCODE 20    ; Virtualization
ISR_ERRCODE   21    ; Control protection (HAS error code)
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Hypervisor injection
ISR_NOERRCODE 29    ; VMM communication
ISR_ERRCODE   30    ; Security exception (HAS error code)
ISR_NOERRCODE 31    ; Reserved

; Define IRQs (remapped to INT 32+)
IRQ 0, 32           ; Timer (PIT)
IRQ 1, 33           ; Keyboard

isr_common_stub:
    pusha                    ; Save all general-purpose registers
    mov ax, ds              ; Save data segment
    push eax
    
    mov ax, 0x10            ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp                ; Pass pointer to registers structure
    call isr_handler        ; Call C handler
    add esp, 4              ; Remove the parameter
    
    pop eax                 ; Restore original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                    ; Restore all registers
    add esp, 8              ; Clean up error code and ISR number
    iret                    ; Return from interrupt

irq_common_stub:
    pusha                    ; Save all general-purpose registers (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    mov ax, ds              ; Save data segment
    push eax
    
    mov ax, 0x10            ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp                ; Pass pointer to registers structure
    call irq_handler        ; Call C handler
    add esp, 4              ; Remove the parameter
    
    pop eax                 ; Restore original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                    ; Restore all registers
    add esp, 8              ; Clean up error code and IRQ number
    iret                    ; Return from interrupt
