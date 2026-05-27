; paging.asm — minimal CR3/CR0 helpers used during early paging bringup.

[BITS 32]

global load_page_directory
global enable_paging

; load_page_directory(pd_phys): CR3 ← arg.
load_page_directory:
    push ebp
    mov ebp, esp
    mov eax, [ebp + 8]
    mov cr3, eax
    pop ebp
    ret

; enable_paging(): CR0.PG = 1 (bit 31).
enable_paging:
    push ebp
    mov ebp, esp
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    pop ebp
    ret
