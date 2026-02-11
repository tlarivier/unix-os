#ifndef KERNEL_DEBUG_H
#define KERNEL_DEBUG_H

#include <kernel/kernel.h>

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define BUG_ON(cond) do { \
    if (unlikely(cond)) { \
        kprintf("\n[BUG] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        while(1) __asm__ volatile("hlt"); \
    } \
} while(0)

#define WARN_ON(cond) ({ \
    int __ret = !!(cond); \
    if (unlikely(__ret)) { \
        kprintf("[WARN] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
    __ret; \
})

#define WARN_ON_ONCE(cond) ({ \
    static int __warned = 0; \
    int __ret = !!(cond); \
    if (unlikely(__ret) && !__warned) { \
        __warned = 1; \
        kprintf("[WARN] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
    __ret; \
})

#define BUILD_BUG_ON(cond) ((void)sizeof(char[1 - 2*!!(cond)]))

enum kern_log_level {
    KERN_EMERG   = 0,
    KERN_ALERT   = 1,
    KERN_CRIT    = 2,
    KERN_ERR     = 3,
    KERN_WARNING = 4,
    KERN_NOTICE  = 5,
    KERN_INFO    = 6,
    KERN_DEBUG   = 7
};

extern int kernel_log_level;

#define pr_emerg(fmt, ...) \
    kprintf("[EMERG] " fmt, ##__VA_ARGS__)

#define pr_alert(fmt, ...) \
    kprintf("[ALERT] " fmt, ##__VA_ARGS__)

#define pr_crit(fmt, ...) \
    kprintf("[CRIT] " fmt, ##__VA_ARGS__)

#ifdef pr_err
#undef pr_err
#endif
#define pr_err(fmt, ...) \
    kprintf("[ERROR] " fmt, ##__VA_ARGS__)

#ifdef pr_warn
#undef pr_warn
#endif
#define pr_warn(fmt, ...) \
    do { if (kernel_log_level >= KERN_WARNING) \
        kprintf("[WARN] " fmt, ##__VA_ARGS__); } while(0)

#define pr_notice(fmt, ...) \
    do { if (kernel_log_level >= KERN_NOTICE) \
        kprintf("[NOTICE] " fmt, ##__VA_ARGS__); } while(0)

#ifdef pr_info
#undef pr_info
#endif
#define pr_info(fmt, ...) \
    do { if (kernel_log_level >= KERN_INFO) \
        kprintf("[INFO] " fmt, ##__VA_ARGS__); } while(0)

#ifdef pr_debug
#undef pr_debug
#endif
#define pr_debug(fmt, ...) \
    do { if (kernel_log_level >= KERN_DEBUG) \
        kprintf("[DEBUG] " fmt, ##__VA_ARGS__); } while(0)

#ifdef DEBUG
#define TRACE_ENTER() pr_debug("%s: enter\n", __func__)
#define TRACE_EXIT()  pr_debug("%s: exit\n", __func__)
#else
#define TRACE_ENTER() do {} while(0)
#define TRACE_EXIT()  do {} while(0)
#endif

#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#endif
