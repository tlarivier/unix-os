#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include <stdint.h>
#include <stddef.h>

struct process;
typedef struct process process_t;

void kernel_main(void);
void kprintf(const char* format, ...);
void kputs(const char* str);

extern void vga_init(void);
extern void vga_print_at(const char* str, int x, int y, uint8_t attr);
extern void vga_putchar(char c);
extern void vga_print(const char* str);
extern void clear_screen(void);
extern void set_cursor_pos(int x, int y);
extern void get_cursor_pos(int* x, int* y);

extern void idt_init(void);
extern void error_init(void);
extern void process_init(void);
extern void scheduler_init(void);
extern void scheduler_add_process(process_t* proc);
extern void schedule(void);
extern void scheduler_remove_process(process_t* proc);
extern void show_process_info(void);
extern void test_process_creation(void);
extern uint32_t get_timer_ticks(void);

extern process_t* get_current_process(void);
extern void context_switch(process_t* old_proc, process_t* new_proc);
extern process_t* create_process(const char* name, uint32_t stack_base);
void process_exit(int exit_code);

extern void gdt_init(void);
extern void gdt_setup_user_segments(void);
extern void tss_init(void);
extern void tss_set_kernel_stack(uint32_t stack);

void heap_init(void);
void memory_init(void);

void sleep_ms(uint32_t ms);
uint32_t get_seconds(void);

void memory_protection_init(void);
void paging_init(void);

void kernel_error_init(void);
int32_t kernel_error_report(int32_t code, const char* message, const char* file, uint32_t line);
void kernel_warning(const char* message, const char* file, uint32_t line);
void kernel_panic(const char* message, const char* file, uint32_t line);
void show_error_statistics(void);
int32_t validate_memory_range(void* ptr, size_t size, uint32_t expected_flags);

#define KERNEL_ERROR(code, msg) kernel_error_report(code, msg, __FILE__, __LINE__)
#define KERNEL_WARNING(msg) kernel_warning(msg, __FILE__, __LINE__)
#define KERNEL_PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)

#ifndef pr_info
#define pr_info(fmt, ...)  kprintf("[INFO] " fmt, ##__VA_ARGS__)
#endif
#ifndef pr_warn
#define pr_warn(fmt, ...)  kprintf("[WARN] " fmt, ##__VA_ARGS__)
#endif
#ifndef pr_err
#define pr_err(fmt, ...)   kprintf("[ERR]  " fmt, ##__VA_ARGS__)
#endif
#ifndef pr_debug
#define pr_debug(fmt, ...) kprintf("[DBG]  " fmt, ##__VA_ARGS__)
#endif

extern void syscall_init(void);

int install_init_binary(void);
void test_userspace(void);

extern int32_t vfs_init(void);
extern int ata_init(void);

extern int ext2_init(void);
extern int ext2_mount_root(void);
extern struct vfs_inode *ext2_get_root_inode(void);

extern int32_t kernel_security_init(void);
extern void run_security_tests(void);
extern void security_audit_report(void);

#endif
