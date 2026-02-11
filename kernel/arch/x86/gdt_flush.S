.section .text
.global gdt_flush
.global tss_flush

# Load new GDT
gdt_flush:
    movl 4(%esp), %eax      # Get GDT pointer from parameter
    lgdt (%eax)             # Load GDT
    
    movw $0x10, %ax         # Load kernel data segment (0x10)
    movw %ax, %ds           # Set data segments
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    # Far jump to reload code segment
    ljmp $0x08, $.gdt_flush_done
    
.gdt_flush_done:
    ret

# Load TSS
tss_flush:
    movw $0x28, %ax         # TSS selector (0x28)
    ltr %ax                 # Load TSS register
    ret
