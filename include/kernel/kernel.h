#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include <stddef.h>
#include <stdint.h>

struct process;
typedef struct process process_t;

void kprintf(const char *format, ...);

extern void vga_init(void);
extern void vga_print_at(const char *str, int x, int y, uint8_t attr);
extern void vga_putchar(char c);
extern void process_init(void);
extern uint32_t get_timer_ticks(void);

extern process_t *get_current_process(void);
void process_exit(int exit_code);

#include <kernel/panic.h>

void sleep_ms(uint32_t ms);
uint32_t get_seconds(void);

void start_init_process(void);

extern int32_t vfs_init(void);

#endif
