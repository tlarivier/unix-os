.section .text
.global userspace_entry
.global kernel_entry_from_userspace

# Switch from kernel (ring 0) to userspace (ring 3)
# Parameters:
#   entry_point    - userspace entry point address
#   user_stack     - userspace stack pointer  
#   user_code_sel  - user code segment selector
#   user_data_sel  - user data segment selector
userspace_entry:
    # Get parameters from stack
    movl 4(%esp), %eax      # entry_point
    movl 8(%esp), %ebx      # user_stack
    movl 12(%esp), %ecx     # user_code_sel
    movl 16(%esp), %edx     # user_data_sel
    
    # Set up user data segments
    movw %dx, %ds           # Set user data segment
    movw %dx, %es
    movw %dx, %fs
    movw %dx, %gs
    
    # Push values for IRET to switch to ring 3
    pushl %edx              # User data segment (SS)
    pushl %ebx              # User stack pointer (ESP)
    pushfl                  # Current flags (EFLAGS)
    orl $0x200, (%esp)      # Ensure interrupts enabled in userspace
    pushl %ecx              # User code segment (CS)
    pushl %eax              # User entry point (EIP)
    
    # Clear general purpose registers for security
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    
    # Jump to userspace (ring 3)
    iret

# Entry point for returning from userspace to kernel
# This is called via system calls or interrupts
kernel_entry_from_userspace:
    # Save all registers
    pushal
    
    # Save user data segments
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    
    # Load kernel segments
    movw $0x10, %ax         # Kernel data segment
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    # Call kernel handler (will be set up by syscall/interrupt system)
    # For now, just return to kernel
    
    # Restore user segments
    popl %gs
    popl %fs
    popl %es
    popl %ds
    
    # Restore registers
    popal
    
    # Return to userspace
    iret
