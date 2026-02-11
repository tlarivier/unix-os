#include <stdint.h>
#include <stdbool.h>
#include <kernel/kernel.h>
#include <kernel/errno.h>
#include <kernel/constants.h>

static uint32_t stack_canary = STACK_CANARY_VALUE;
static bool security_initialized = false;

int32_t kernel_security_init(void) {
    uint32_t tsc_low, tsc_high;
    __asm__ volatile("rdtsc" : "=a"(tsc_low), "=d"(tsc_high));
    stack_canary = tsc_low ^ tsc_high ^ STACK_CANARY_VALUE;
    
    security_initialized = true;
    return 0;
}

uint32_t security_get_canary(void) {
    return stack_canary;
}

bool security_check_canary(uint32_t value) {
    return value == stack_canary;
}

bool validate_user_pointer(const void* ptr, size_t size) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t end = addr + size;
    if (addr < USER_SPACE_START || end > KERNEL_BASE) return false;
    if (end < addr) return false;
    return true;
}

bool validate_kernel_pointer(const void* ptr, size_t size) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t end = addr + size;
    if (addr >= KERNEL_BASE || (addr >= 0x100000 && addr < 0x400000)) {
        if (end < addr) return false;
        return true;
    }
    return false;
}
