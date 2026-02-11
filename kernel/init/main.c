#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>
#include <kernel/vga_graphics.h>
#include <kernel/gdt.h>
#include <kernel/vfs.h>
#include <kernel/elf_loader.h>
#include <kernel/errno.h>
#include <kernel/constants.h>
#include <kernel/kernel_state.h>
#include <kernel/kprintf.h>
#include <kernel/io.h>
#include <kernel/debug.h>
#include <kernel/random.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/ipc.h>
#include <kernel/serial.h>

extern int install_userspace_binaries(void);

int kernel_log_level = KERN_INFO;

static kernel_state_t main_kernel_state = {0};
kernel_state_t *kernel_state = &main_kernel_state;

void init_stack_canary(void) {
    kernel_state->stack_canary = STACK_CANARY_VALUE;
}

void check_stack_canary(void) {
    process_check_current_canary();
}

void enable_stack_canary_warnings(void) {
    kernel_state->vga_ready = true;
}



#ifdef DEBUG
void test_interrupts(void) {
    kprintf("Testing interrupt system...\n");
    kprintf("Timer test: counting ticks...\n");
    
    uint32_t start_ticks = get_timer_ticks();
    uint32_t current_ticks = start_ticks;
    int timeout = 0;
    
    while (current_ticks == start_ticks && timeout < 1000000) {
        current_ticks = get_timer_ticks();
        timeout++;
        __asm__ volatile("pause");
    }
    
    if (current_ticks > start_ticks) {
        kprintf("Timer working! Ticks: ");
        kprintf("%u", current_ticks);
        kprintf("\n");
    } else {
        kprintf("Timer not responding!\n");
    }
    
    kprintf("Keyboard test: press keys to see scancodes\n");
    kprintf("System ready - interrupts enabled\n");
}
#endif


void kmain(void) {
    serial_init();
    init_stack_canary();
    random_init();
    vga_init();
    vga_clear();
    kprintf("Unix-like OS Booting...\n");
    
    enable_stack_canary_warnings();
    gdt_init();
    gdt_setup_user_segments();
    tss_init();
    idt_init();
    keyboard_init();
    timer_init(100);
    kernel_error_init();
    check_stack_canary();
    process_init();
    scheduler_init();
    memory_init();
    memory_protection_init();
    paging_init();
    kernel_security_init();
    ata_init();
    vfs_init();
    
    vfs_mkdir("/sbin", 0755);
    vfs_mkdir("/bin",  0755);
    vfs_mkdir("/dev",  0755);
    vfs_mkdir("/tmp",  0777);
    
    install_userspace_binaries();
    
    syscall_init();
    pipe_subsystem_init();
    
    process_t *kproc = get_current_process();
    if (kproc) {
        scheduler_add_process(kproc);
    }
    scheduler_enable();
    
    test_userspace();
    
    kprintf("ERROR: Returned from userspace!\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}
