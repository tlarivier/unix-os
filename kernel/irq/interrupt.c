#include <stdint.h>
#include <kernel/drivers.h>
#include <kernel/interrupt.h>
#include <kernel/kernel.h>
#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <../uapi/signal.h>

#define PANIC_TEXT_ATTR 0x0C
#define PANIC_BACKGROUND_ATTR 0x4000
#define STACK_DUMP_ENTRIES 8
#define KERNEL_STACK_BOUNDARY 0xC0400000
#define PF_PRESENT_BIT 0x1
#define PF_WRITE_BIT 0x2
#define PF_USER_BIT 0x4

static void print_hex_at(uint32_t value, int x, int y, uint8_t attr) {
    char hex[] = "0123456789ABCDEF";
    char buffer[9];
    buffer[8] = '\0';
    
    for (int i = 7; i >= 0; i--) {
        buffer[i] = hex[value & 0xF];
        value >>= 4;
    }
    
    vga_print_at(buffer, x, y, attr);
}

static void print_decimal_at(uint32_t value, int x, int y, uint8_t attr) {
    char buffer[11];
    int i = 0;
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    buffer[i] = '\0';
    
    for (int j = 0; j < i / 2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = temp;
    }
    
    vga_print_at(buffer, x, y, attr);
}

static void print_stack_trace(registers_t* regs) {
    vga_print_at("=== REGISTER DUMP ===", 0, 10, PANIC_TEXT_ATTR);
    
    vga_print_at("EAX:", 0, 11, PANIC_TEXT_ATTR); print_hex_at(regs->eax, 4, 11, PANIC_TEXT_ATTR);
    vga_print_at("EBX:", 20, 11, PANIC_TEXT_ATTR); print_hex_at(regs->ebx, 24, 11, PANIC_TEXT_ATTR);
    vga_print_at("ECX:", 40, 11, PANIC_TEXT_ATTR); print_hex_at(regs->ecx, 44, 11, PANIC_TEXT_ATTR);
    vga_print_at("EDX:", 60, 11, PANIC_TEXT_ATTR); print_hex_at(regs->edx, 64, 11, PANIC_TEXT_ATTR);
    
    vga_print_at("ESI:", 0, 12, PANIC_TEXT_ATTR); print_hex_at(regs->esi, 4, 12, PANIC_TEXT_ATTR);
    vga_print_at("EDI:", 20, 12, PANIC_TEXT_ATTR); print_hex_at(regs->edi, 24, 12, PANIC_TEXT_ATTR);
    vga_print_at("EBP:", 40, 12, PANIC_TEXT_ATTR); print_hex_at(regs->ebp, 44, 12, PANIC_TEXT_ATTR);
    vga_print_at("ESP:", 60, 12, PANIC_TEXT_ATTR); print_hex_at(regs->esp, 64, 12, PANIC_TEXT_ATTR);
    
    vga_print_at("EIP:", 0, 13, PANIC_TEXT_ATTR); print_hex_at(regs->eip, 4, 13, PANIC_TEXT_ATTR);
    vga_print_at("CS:", 20, 13, PANIC_TEXT_ATTR); print_hex_at(regs->cs, 24, 13, PANIC_TEXT_ATTR);
    vga_print_at("EFLAGS:", 40, 13, PANIC_TEXT_ATTR); print_hex_at(regs->eflags, 47, 13, PANIC_TEXT_ATTR);
    
    vga_print_at("=== STACK DUMP ===", 0, 15, PANIC_TEXT_ATTR);
    uint32_t* stack = (uint32_t*)regs->esp;
    
    for (int i = 0; i < STACK_DUMP_ENTRIES && (uint32_t)&stack[i] < KERNEL_STACK_BOUNDARY; i++) {
        vga_print_at("STK+", 0, 16 + i, PANIC_TEXT_ATTR);
        print_decimal_at(i * 4, 4, 16 + i, PANIC_TEXT_ATTR);
        vga_print_at(":", 7, 16 + i, PANIC_TEXT_ATTR);
        print_hex_at(stack[i], 9, 16 + i, PANIC_TEXT_ATTR);
    }
}

void isr_handler(registers_t* regs) {
    if (regs->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r" (cr2));
        uint32_t err_code = regs->err_code;
        
        /* COW: If write fault on present page, try COW handler */
        if ((err_code & 0x01) && (err_code & 0x02)) {  
            extern int handle_cow_fault(uint32_t fault_addr);
            if (handle_cow_fault(cr2) == 0) {
                return;  
            }
        }
        
        if (!(err_code & 0x01)) {  
            extern int handle_demand_fault(uint32_t fault_addr);
            if (handle_demand_fault(cr2) == 0) {
                return;  
            }
        }
    }
    
    if ((regs->cs & 0x3) == 3) {
        kprintf("\n[SEGFAULT] Process crashed at EIP=0x%x\n", regs->eip);
        kprintf("  Fault type: %d", regs->int_no);
        if (regs->int_no == 14) {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r" (cr2));
            kprintf(", Address: 0x%x", cr2);
        }
        kprintf("\n");
        process_t* current = get_current_process();
        if (current && current->pid != 0) {
            kprintf("[KERNEL] Terminating PID %d\n", current->pid);
            process_exit(-(128 + SIGSEGV));  /* Standard signal exit code */
            schedule();
            kprintf("[KERNEL] No more processes. System idle.\n");
            while(1) __asm__ volatile("hlt");
        }
        /* Kernel fault in userspace context but no process - idle */
        while(1) __asm__ volatile("hlt");
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        ((uint16_t*)VGA_MEMORY)[i] = PANIC_BACKGROUND_ATTR;
    }
    
    vga_print_at("*** KERNEL PANIC ***", 25, 0, PANIC_TEXT_ATTR);
    
    switch (regs->int_no) {
        case 0:
            vga_print_at("Division by Zero Exception", 20, 1, PANIC_TEXT_ATTR);
            break;
        case 13: {
            vga_print_at("General Protection Fault", 20, 1, PANIC_TEXT_ATTR);
            vga_print_at("Error Code: 0x", 10, 2, PANIC_TEXT_ATTR);
            print_hex_at(regs->err_code, 24, 2, PANIC_TEXT_ATTR);
            /* Show which process crashed */
            process_t* p = get_current_process();
            if (p) {
                vga_print_at("PID:", 35, 2, PANIC_TEXT_ATTR);
                print_decimal_at(p->pid, 39, 2, PANIC_TEXT_ATTR);
            }
            break;
        }
        case 14: {
                uint32_t fault_addr;
                __asm__ volatile("mov %%cr2, %0" : "=r" (fault_addr));
                
                vga_print_at("PAGE FAULT! Addr: ", 2, 2, PANIC_TEXT_ATTR);
                print_hex_at(fault_addr, 21, 2, PANIC_TEXT_ATTR);
                vga_print_at("Details:", 40, 2, PANIC_TEXT_ATTR);
                if (regs->err_code & PF_PRESENT_BIT) vga_print_at("PRESENT ", 48, 2, PANIC_TEXT_ATTR);
                if (regs->err_code & PF_WRITE_BIT) vga_print_at("WRITE ", 56, 2, PANIC_TEXT_ATTR);
                if (regs->err_code & PF_USER_BIT) vga_print_at("USER ", 62, 2, PANIC_TEXT_ATTR);
                
                /* Debug: show CR3 only (safe) */
                uint32_t cr3;
                __asm__ volatile("mov %%cr3, %0" : "=r" (cr3));
                vga_print_at("CR3:", 2, 6, PANIC_TEXT_ATTR);
                print_hex_at(cr3, 7, 6, PANIC_TEXT_ATTR);
            }
            break;
        default:
            vga_print_at("Unknown Exception: ", 15, 1, PANIC_TEXT_ATTR);
            print_decimal_at(regs->int_no, 33, 1, PANIC_TEXT_ATTR);
            break;
    }
    vga_print_at("Crash at EIP: 0x", 2, 4, PANIC_TEXT_ATTR);
    print_hex_at(regs->eip, 19, 4, PANIC_TEXT_ATTR);
    
    vga_print_at("Timer Ticks: ", 2, 5, PANIC_TEXT_ATTR);
    print_decimal_at(get_timer_ticks(), 15, 5, PANIC_TEXT_ATTR);
    extern void check_stack_canary(void);
    check_stack_canary();
    print_stack_trace(regs);
    
    vga_print_at("=== SYSTEM HALTED ===", 25, 24, PANIC_TEXT_ATTR);
    __asm__ volatile("cli");
    while(1) __asm__ volatile("hlt");
}

void irq_handler(registers_t* regs) {
    if (regs->int_no < 32 || regs->int_no > 47) return;
    if (regs->int_no >= 40) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
    
    switch (regs->int_no) {
        case IRQ_TIMER:
            timer_handle_irq();
            break;
            
        case IRQ_KEYBOARD:
            keyboard_handle_irq();
            break;
            
        default:
            break;
    }
    
    /* Preemptive scheduling from interrupts causes stack corruption.
     * Use cooperative scheduling: reschedule happens via yield() or 
     * blocking syscalls (waitpid, read, etc.) only. */
}
