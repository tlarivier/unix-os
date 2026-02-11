.section .text
.global asm_context_switch

# void asm_context_switch(process_context_t* old_ctx, process_context_t* new_ctx)
#
# Saves current CPU state to old_ctx and restores from new_ctx
#
# process_context_t layout (40 bytes):
#   offset 0:  eax
#   offset 4:  ebx  
#   offset 8:  ecx
#   offset 12: edx
#   offset 16: esi
#   offset 20: edi
#   offset 24: ebp
#   offset 28: esp
#   offset 32: eip
#   offset 36: eflags
#   (segments not used for kernel-to-kernel switch)

asm_context_switch:
    # Get parameters
    movl 4(%esp), %eax      # old_ctx
    movl 8(%esp), %edx      # new_ctx
    
    # Check for null pointers
    testl %eax, %eax
    jz .load_new
    testl %edx, %edx
    jz .done
    
    # === Save current context to old_ctx ===
    
    # Save general purpose registers
    movl %ebx, 4(%eax)      # save ebx
    movl %ecx, 8(%eax)      # save ecx
    movl %esi, 16(%eax)     # save esi
    movl %edi, 20(%eax)     # save edi
    movl %ebp, 24(%eax)     # save ebp
    
    # Save stack pointer (current esp + 4 to skip return address)
    movl %esp, %ecx
    addl $4, %ecx
    movl %ecx, 28(%eax)     # save esp
    
    # Save return address as eip (where we'll resume)
    movl (%esp), %ecx
    movl %ecx, 32(%eax)     # save eip (return address)
    
    # Save flags
    pushfl
    popl %ecx
    movl %ecx, 36(%eax)     # save eflags
    
    # Save eax last (we used it as pointer)
    movl $0, 0(%eax)        # eax will be 0 on resume
    
.load_new:
    # Check new_ctx not null
    testl %edx, %edx
    jz .done
    
    # === Restore context from new_ctx ===
    
    # Restore flags first
    movl 36(%edx), %eax
    pushl %eax
    popfl
    
    # Restore general purpose registers
    movl 4(%edx), %ebx      # restore ebx
    movl 8(%edx), %ecx      # restore ecx  
    movl 16(%edx), %esi     # restore esi
    movl 20(%edx), %edi     # restore edi
    movl 24(%edx), %ebp     # restore ebp
    
    # Get new stack and eip
    movl 28(%edx), %esp     # restore esp
    movl 32(%edx), %eax     # get eip to jump to
    
    # Restore edx last (we used it as pointer)
    pushl %eax              # save eip on new stack
    movl 12(%edx), %edx     # restore edx
    movl 0(%esp), %eax      # get eip back
    addl $4, %esp           # clean stack
    
    # Jump to new context
    jmp *%eax
    
.done:
    ret
