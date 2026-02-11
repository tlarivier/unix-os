[BITS 32]

global load_page_directory
global enable_paging

; Load page directory address into CR3
load_page_directory:
    push ebp
    mov ebp, esp
    mov eax, [ebp + 8]    ; Get page directory address from first argument
    mov cr3, eax          ; Load it into CR3 register
    pop ebp
    ret

; Enable paging by setting bit 31 of CR0
enable_paging:
    push ebp
    mov ebp, esp
    mov eax, cr0          ; Get current CR0 value
    or eax, 0x80000000    ; Set bit 31 (paging enable)
    mov cr0, eax          ; Write back to CR0
    pop ebp
    ret
