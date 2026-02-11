.section .text
.global syscall_entry_point

# Syscall entry point (INT 0x80)
# Stack on entry (from Ring 3): SS, ESP, EFLAGS, CS, EIP
syscall_entry_point:
    # Push dummy error code and interrupt number to match structure
    pushl $0            # err_code (dummy)
    pushl $0x80         # int_no (syscall = 0x80)
    
    # Save all general registers (order: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    pushal
    
    # Save data segment
    pushl %ds
    
    # Load kernel data segments
    movw $0x10, %ax     # Kernel data segment
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    # Push pointer to register structure
    pushl %esp
    
    # Call C syscall handler
    call syscall_handler
    
    # Remove stack parameter
    addl $4, %esp
    
    # Restore data segment
    popl %ds
    
    # Restore all registers
    popal
    
    # Remove int_no and err_code
    addl $8, %esp
    
    # Return to userspace
    iret
