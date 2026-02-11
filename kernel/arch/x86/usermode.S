.section .text
.global jump_to_usermode

# void jump_to_usermode(uint32_t entry_point, uint32_t user_stack)
jump_to_usermode:
    # Disable interrupts during setup
    cli
    
    # Get arguments BEFORE changing any segments
    movl 4(%esp), %eax      # entry_point
    movl 8(%esp), %ebx      # user_stack
    
    # Build IRET frame
    # Stack must be: SS, ESP, EFLAGS, CS, EIP (from bottom to top)
    
    pushl $0x23             # SS = user data segment (0x20 | RPL 3)
    pushl %ebx              # ESP = user stack
    pushl $0x202            # EFLAGS - IF=1 (interrupts enabled), reserved bit 1=1
    pushl $0x1B             # CS = user code segment (0x18 | RPL 3)  
    pushl %eax              # EIP = entry point
    
    # Now set data segments to user mode
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    # Clear registers for security
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    
    # Jump to ring 3
    iret
