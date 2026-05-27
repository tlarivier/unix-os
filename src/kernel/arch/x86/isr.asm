; isr.asm — CPU exception (0..31), IRQ, IPI and LAPIC-timer stubs.
; Each stub pushes (err_code, vector), saves caller regs+segments, loads kernel
; data segs + per-CPU %gs (computed from LAPIC ID), calls the C handler with
; a registers_t*, then restores and IRETs.

[BITS 32]

extern isr_handler
extern irq_handler

; Per-CPU %gs descriptor base in the GDT. We compute selector =
; GDT_GS_BASE_SEL + lapic_id*8 by reading LAPIC ID at MMIO 0xFEE00020 bits
; 24..31. Assumes cpu_id == lapic_id (true on QEMU; needs a remap table on
; real iron with sparse APIC IDs — see <kernel/percpu.h>).
%define GDT_GS_BASE_SEL  0x68
%define LAPIC_ID_MMIO    0xFEE00020
%define KERNEL_DATA_SEL  0x10

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                  ; CPU didn't push err_code — fake one for uniform frame
    push %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                 ; err_code already on stack from CPU
    jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common_stub
%endmacro

%macro LOAD_PERCPU_GS 0
    mov eax, [LAPIC_ID_MMIO]
    shr eax, 24
    shl eax, 3
    add eax, GDT_GS_BASE_SEL
    mov gs, ax
%endmacro

; %1 = stub label, %2 = C handler symbol
%macro COMMON_STUB 2
%1:
    pusha
    mov ax, ds              ; ds → into struct
    push eax
    push gs                 ; caller's gs → above struct, restored at exit

    mov ax, KERNEL_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    LOAD_PERCPU_GS

    lea eax, [esp + 4]      ; &regs — skip the saved gs above esp
    push eax
    call %2
    add esp, 4

    pop gs
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax

    popa
    add esp, 8              ; drop err_code + vector
    iret
%endmacro

; CPU exceptions 0..31. "_ERRCODE" = CPU pushes an error code.
ISR_NOERRCODE 0     ; #DE  divide by zero
ISR_NOERRCODE 1     ; #DB  debug
ISR_NOERRCODE 2     ;      NMI
ISR_NOERRCODE 3     ; #BP  breakpoint
ISR_NOERRCODE 4     ; #OF  overflow
ISR_NOERRCODE 5     ; #BR  bound range
ISR_NOERRCODE 6     ; #UD  invalid opcode
ISR_NOERRCODE 7     ; #NM  device not available
ISR_ERRCODE   8     ; #DF  double fault
ISR_NOERRCODE 9     ;      coprocessor segment overrun
ISR_ERRCODE   10    ; #TS  invalid TSS
ISR_ERRCODE   11    ; #NP  segment not present
ISR_ERRCODE   12    ; #SS  stack-segment fault
ISR_ERRCODE   13    ; #GP  general protection
ISR_ERRCODE   14    ; #PF  page fault
ISR_NOERRCODE 15    ;      reserved
ISR_NOERRCODE 16    ; #MF  x87 FPU
ISR_ERRCODE   17    ; #AC  alignment check
ISR_NOERRCODE 18    ; #MC  machine check
ISR_NOERRCODE 19    ; #XM  SIMD FP
ISR_NOERRCODE 20    ; #VE  virtualization
ISR_ERRCODE   21    ; #CP  control protection
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28    ; #HV  hypervisor injection
ISR_NOERRCODE 29    ; #VC  VMM communication
ISR_ERRCODE   30    ; #SX  security exception
ISR_NOERRCODE 31

; PIC IRQs (remapped to 32+)
IRQ 0, 32           ; PIT timer
IRQ 1, 33           ; keyboard
IRQ 4, 36           ; COM1 serial

; LAPIC IPIs — irq_handler routes 0xFB..0xFD to ipi_dispatch.
IRQ 251, 0xFB       ; IPI CALL_FUNCTION
IRQ 252, 0xFC       ; IPI RESCHED
IRQ 253, 0xFD       ; IPI TLB_FLUSH

IRQ 239, 0xEF       ; LAPIC timer tick

COMMON_STUB isr_common_stub, isr_handler
COMMON_STUB irq_common_stub, irq_handler
