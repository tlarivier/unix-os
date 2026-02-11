#ifndef _KERNEL_RCU_H
#define _KERNEL_RCU_H

#include <stdint.h>

/*
 * RCU (Read-Copy-Update) - Lock-free read-side synchronization
 * 
 * Pattern: Readers access data without locks, writers wait for
 * all readers to finish before freeing old data.
 * 
 * On single-core x86, this is simplified:
 * - Readers just need memory barriers
 * - Writers wait for a context switch (quiescent state)
 * 
 * Usage:
 *   Reader:
 *     rcu_read_lock();
 *     data = rcu_dereference(ptr);
 *     // use data...
 *     rcu_read_unlock();
 *   
 *   Writer:
 *     new_data = alloc_and_init();
 *     old_data = ptr;
 *     rcu_assign_pointer(ptr, new_data);
 *     synchronize_rcu();
 *     kfree(old_data);
 */

/* RCU read-side critical section markers */
/* On single-core, these are essentially no-ops but provide documentation */
static inline void rcu_read_lock(void) {
    /* Prevent preemption - on single-core x86 with disabled preemption, nothing needed */
    __asm__ volatile("" ::: "memory");
}

static inline void rcu_read_unlock(void) {
    __asm__ volatile("" ::: "memory");
}

/* Dereference an RCU-protected pointer with proper barrier */
#define rcu_dereference(p) ({ \
    typeof(p) _p = (p); \
    __sync_synchronize(); \
    _p; \
})

/* Assign to an RCU-protected pointer with proper barrier */
#define rcu_assign_pointer(p, v) do { \
    __sync_synchronize(); \
    (p) = (v); \
} while (0)

/* Wait for all readers to exit RCU critical sections */
/* On single-core, a context switch guarantees this */
void synchronize_rcu(void);

/*
 * RCU callback infrastructure
 * For deferred freeing without blocking
 */
typedef void (*rcu_callback_t)(void *arg);

struct rcu_head {
    struct rcu_head *next;
    rcu_callback_t func;
    void *arg;
};

/* Schedule callback after grace period */
void call_rcu(struct rcu_head *head, rcu_callback_t func, void *arg);

/* Process pending RCU callbacks (called from scheduler) */
void rcu_process_callbacks(void);

/*
 * RCU-protected list operations
 * Safe iteration while allowing concurrent modifications
 */
#define list_for_each_entry_rcu(pos, head, member) \
    for (pos = rcu_dereference(list_first_entry(head, typeof(*pos), member)); \
         &pos->member != (head); \
         pos = rcu_dereference(list_next_entry(pos, member)))

/* Add to list with RCU safety */
#define list_add_rcu(new, head) do { \
    (new)->next = (head)->next; \
    (new)->prev = (head); \
    __sync_synchronize(); \
    (head)->next->prev = (new); \
    (head)->next = (new); \
} while (0)

/* Delete from list with RCU safety (doesn't free) */
#define list_del_rcu(entry) do { \
    (entry)->prev->next = (entry)->next; \
    (entry)->next->prev = (entry)->prev; \
} while (0)

#endif /* _KERNEL_RCU_H */
