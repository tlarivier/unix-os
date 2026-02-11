#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

/*
 * Spinlock implementation - SMP SAFETY
 * 
 * "Simple and correct beats complex and fast" - Linus
 * These are the building blocks for all kernel synchronization
 */

typedef struct spinlock {
    volatile uint32_t locked;   // 0 = unlocked, 1 = locked
    const char *name;           // For debugging
    uint32_t cpu;               // CPU that holds the lock
} spinlock_t;

/* Static spinlock initializer */
#define SPINLOCK_INIT(name_str) { .locked = 0, .name = name_str, .cpu = 0 }

/* Dynamic spinlock initialization */
static inline void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->name = name;
    lock->cpu = 0;
}

/* Acquire spinlock - ATOMIC */
static inline void spin_lock(spinlock_t *lock) {
    // Disable interrupts to prevent deadlocks
    __asm__ volatile("cli");
    
    // Try to acquire lock atomically
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        // Spin wait with pause instruction (Intel optimization)
        __asm__ volatile("pause" ::: "memory");
    }
    
    // Memory barrier - prevent reordering
    __asm__ volatile("" ::: "memory");
}

/* Release spinlock */
static inline void spin_unlock(spinlock_t *lock) {
    // Memory barrier
    __asm__ volatile("" ::: "memory");
    
    // Release lock
    __sync_lock_release(&lock->locked);
    
    // Re-enable interrupts
    __asm__ volatile("sti");
}

/* Try to acquire lock without blocking */
static inline int spin_trylock(spinlock_t *lock) {
    __asm__ volatile("cli");
    
    if (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile("sti");
        return 0;  // Failed to acquire
    }
    
    __asm__ volatile("" ::: "memory");
    return 1;  // Successfully acquired
}

/* Check if lock is held */
static inline int spin_is_locked(spinlock_t *lock) {
    return lock->locked;
}

/*
 * IRQ-safe spinlock variants
 * These save/restore interrupt state instead of unconditionally enabling
 */

/* Acquire spinlock, saving interrupt state */
static inline void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags) {
    /* Save EFLAGS and disable interrupts atomically */
    __asm__ volatile(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(*flags)
        :
        : "memory"
    );
    
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }
    
    __asm__ volatile("" ::: "memory");
}

/* Release spinlock, restoring interrupt state */
static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags) {
    __asm__ volatile("" ::: "memory");
    
    __sync_lock_release(&lock->locked);
    
    /* Restore EFLAGS (including interrupt flag) */
    __asm__ volatile(
        "push %0\n\t"
        "popf"
        :
        : "r"(flags)
        : "memory", "cc"
    );
}

/* Disable interrupts and save state (without lock) */
static inline uint32_t local_irq_save(void) {
    uint32_t flags;
    __asm__ volatile(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

/* Restore interrupt state */
static inline void local_irq_restore(uint32_t flags) {
    __asm__ volatile(
        "push %0\n\t"
        "popf"
        :
        : "r"(flags)
        : "memory", "cc"
    );
}

#endif /* KERNEL_SPINLOCK_H */
