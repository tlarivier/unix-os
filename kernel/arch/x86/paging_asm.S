.section .text
.global enable_paging
.global get_cr3
.global set_cr3

# Enable paging and set page directory
enable_paging:
    pushl %ebp
    movl %esp, %ebp
    
    # Get page directory physical address from parameter
    movl 8(%ebp), %eax
    
    # Set CR3 to page directory
    movl %eax, %cr3
    
    # Enable paging bit in CR0
    movl %cr0, %eax
    orl $0x80000000, %eax    # Set PG bit (bit 31)
    movl %eax, %cr0
    
    popl %ebp
    ret

# Get current CR3 value
get_cr3:
    movl %cr3, %eax
    ret

# Set CR3 to new page directory
set_cr3:
    movl 4(%esp), %eax       # Get parameter
    movl %eax, %cr3          # Set CR3
    ret
