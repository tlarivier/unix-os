#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/constants.h>
#include <kernel/kprintf.h>
#include <stdint.h>

void memory_protection_init(void) { }

int32_t validate_memory_access(void* ptr, size_t size, uint32_t flags) {
    if (!ptr) return -EINVAL;
    uint32_t addr = (uint32_t)ptr;
    if (addr + size < addr) return -EOVERFLOW;
    if ((flags & 0x1) && (addr >= KERNEL_BASE || addr + size > KERNEL_BASE)) return -EACCES;
    return 0;
}

static volatile uint32_t total_allocs = 0;
static volatile uint32_t total_frees = 0;
static volatile uint32_t bytes_allocated = 0;

void memory_track_alloc(size_t size) {
    uint32_t flags;
    __asm__ volatile("pushf; cli; pop %0" : "=r"(flags));
    total_allocs++;
    bytes_allocated += size;
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

void memory_track_free(size_t size) {
    uint32_t flags;
    __asm__ volatile("pushf; cli; pop %0" : "=r"(flags));
    total_frees++;
    if (bytes_allocated >= size) bytes_allocated -= size;
    __asm__ volatile("push %0; popf" : : "r"(flags));
}

void memory_leak_check(void) {
    if (total_allocs != total_frees) {
        kprintf("[MEM] Potential leak: %d allocs, %d frees, %d bytes\n",
                total_allocs, total_frees, bytes_allocated);
    }
}
int32_t setup_stack_guard(void* stack_base, size_t stack_size) { 
    (void)stack_base; (void)stack_size;
    return 0; 
}

int32_t validate_kernel_string(const char* str, size_t max_len) {
    if (!str) return -EINVAL;
    for (size_t i = 0; i < max_len; i++) {
        if (str[i] == '\0') return 0;
    }
    return -EOVERFLOW;
}
