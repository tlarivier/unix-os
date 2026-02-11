.section .text
.global idt_flush

# Load new IDT
idt_flush:
    movl 4(%esp), %eax      # Get IDT pointer from parameter
    lidt (%eax)             # Load IDT
    ret
