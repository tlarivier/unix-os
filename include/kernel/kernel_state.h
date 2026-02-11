#ifndef KERNEL_STATE_H
#define KERNEL_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct kernel_state {
    bool vga_ready;
    bool memory_initialized;
    bool scheduler_enabled;
    bool interrupts_enabled;
    
    volatile uint32_t stack_canary;
    
    uint32_t boot_time;
    uint32_t uptime_seconds;
    uint32_t context_switches;
    uint32_t page_faults;
    uint32_t syscalls_total;
    
    uint32_t current_cpu;
    uint32_t num_cpus;
    
} kernel_state_t;

extern kernel_state_t *kernel_state;

void kernel_state_init(void);
kernel_state_t *get_kernel_state(void);

void init_stack_canary(void);
void check_stack_canary(void);

#endif 
