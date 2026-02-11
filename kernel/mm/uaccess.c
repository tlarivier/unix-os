#include <kernel/uaccess.h>
#include <kernel/constants.h>
#include <kernel/errno.h>
#include <stdint.h>

/*
 * Note: Full page presence check would require walking page tables
 * which is expensive. For now, we validate address range and rely
 * on page fault handler for missing pages.
 */
int access_ok(const void* addr, size_t size) {
    if (!addr) return 0;
    uintptr_t a = (uintptr_t)addr;
    
    if (a >= KERNEL_BASE) return 0;
    if (a + size < a) return 0;
    if (a + size > KERNEL_BASE) return 0;
    if (a < 0x1000) return 0;
    
    return 1;
}

int copy_from_user(void* dst, const void* src, size_t n) {
    if (!dst || !src) return -EINVAL;
    if (n == 0) return 0;
    if (!access_ok(src, n)) return -EFAULT;
    
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return 0;
}

int copy_to_user(void* dst, const void* src, size_t n) {
    if (!dst || !src) return -EINVAL;
    if (n == 0) return 0;
    if (!access_ok(dst, n)) return -EFAULT;
    
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return 0;
}

int copy_str_from_user(char* dst, const char* src, size_t max) {
    if (!dst || !src || max == 0) return -EINVAL;
    /* Validate entire potential range to prevent crossing into kernel space */
    if (!access_ok(src, max)) return -EFAULT;
    
    size_t i;
    for (i = 0; i < max - 1; i++) {
        char c = ((const char*)src)[i];
        dst[i] = c;
        if (c == '\0') return (int)i;
    }
    dst[max - 1] = '\0';
    return -EOVERFLOW;
}
