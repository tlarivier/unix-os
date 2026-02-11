#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>
#include <kernel/scheduler.h>
#include <kernel/uaccess.h>
#include <stdint.h>

#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_WAIT_PRIVATE  128
#define FUTEX_WAKE_PRIVATE  129

typedef struct futex_waiter {
    uint32_t            uaddr;
    process_t*          proc;
    struct futex_waiter* next;
} futex_waiter_t;

#define FUTEX_HASH_SIZE 64
static futex_waiter_t* futex_queues[FUTEX_HASH_SIZE];
static spinlock_t futex_lock;
static int futex_initialized = 0;

static uint32_t futex_hash(uint32_t uaddr) {
    return (uaddr >> 2) % FUTEX_HASH_SIZE;
}

void futex_init(void) {
    if (futex_initialized) return;
    
    spinlock_init(&futex_lock, "futex");
    for (int i = 0; i < FUTEX_HASH_SIZE; i++) {
        futex_queues[i] = NULL;
    }
    futex_initialized = 1;
}

static int futex_wait(uint32_t uaddr, uint32_t val, uint32_t timeout_ptr) {
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    /* FUTEX-003: Parse timeout if provided */
    uint32_t timeout_ticks = 0;
    if (timeout_ptr != 0) {
        uint32_t tv_sec, tv_nsec;
        if (copy_from_user(&tv_sec, (void*)timeout_ptr, sizeof(uint32_t)) < 0 ||
            copy_from_user(&tv_nsec, (void*)(timeout_ptr + 4), sizeof(uint32_t)) < 0) {
            return -EFAULT;
        }
        timeout_ticks = tv_sec * 100 + tv_nsec / 10000000;
        if (timeout_ticks == 0 && (tv_sec || tv_nsec)) {
            timeout_ticks = 1;  /* Minimum 1 tick */
        }
    }
    
    if (!access_ok((void*)uaddr, sizeof(uint32_t))) {
        return -EFAULT;
    }
    
    spin_lock(&futex_lock);
    
    uint32_t current_val;
    if (copy_from_user(&current_val, (void*)uaddr, sizeof(uint32_t)) < 0) {
        spin_unlock(&futex_lock);
        return -EFAULT;
    }
    
    if (current_val != val) {
        spin_unlock(&futex_lock);
        return -EAGAIN;
    }
    
    futex_waiter_t* waiter = kmalloc(sizeof(futex_waiter_t));
    if (!waiter) {
        spin_unlock(&futex_lock);
        return -ENOMEM;
    }
    
    waiter->uaddr = uaddr;
    waiter->proc  = proc;
    
    uint32_t hash = futex_hash(uaddr);
    waiter->next  = futex_queues[hash];
    futex_queues[hash] = waiter;
    
    proc->state = PROCESS_BLOCKED;
    
    /* FUTEX-003: Set alarm for timeout if specified */
    uint32_t old_alarm = proc->alarm_ticks;
    int timed_out = 0;
    if (timeout_ticks > 0) {
        proc->alarm_ticks = timeout_ticks;
    }
    
    spin_unlock(&futex_lock);
    
    extern void schedule(void);
    schedule();
    
    if (timeout_ticks > 0 && proc->alarm_ticks == 0) {
        timed_out = 1;
    }
    proc->alarm_ticks = old_alarm;  
    
    spin_lock(&futex_lock);
    futex_waiter_t** pp = &futex_queues[hash];
    while (*pp) {
        if ((*pp)->proc == proc && (*pp)->uaddr == uaddr) {
            futex_waiter_t* to_free = *pp;
            *pp = (*pp)->next;
            kfree(to_free);
            break;
        }
        pp = &(*pp)->next;
    }
    spin_unlock(&futex_lock);
    
    if (timed_out) {
        return -ETIMEDOUT;
    }
    
    extern int signal_pending_check(void);
    if (signal_pending_check()) {
        return -EINTR;
    }
    
    return 0;
}

static int futex_wake(uint32_t uaddr, uint32_t count) {
    if (!futex_initialized) return 0;
    
    spin_lock(&futex_lock);
    
    uint32_t hash = futex_hash(uaddr);
    futex_waiter_t** pp = &futex_queues[hash];
    int woken = 0;
    
    while (*pp != NULL && (uint32_t)woken < count) {
        futex_waiter_t* w = *pp;
        
        if (w->uaddr == uaddr) {
            w->proc->state = PROCESS_READY;
            
            *pp = w->next;
            kfree(w);
            woken++;
        } else {
            pp = &(*pp)->next;
        }
    }
    
    spin_unlock(&futex_lock);
    
    return woken;
}

int32_t sys_futex(uint32_t uaddr, uint32_t op, uint32_t val, uint32_t timeout, uint32_t uaddr2) {
    (void)uaddr2;  
    
    if (!futex_initialized) futex_init();
    
    int cmd = op & 0x7F;  
    
    switch (cmd) {
        case FUTEX_WAIT:
            return futex_wait(uaddr, val, timeout);
            
        case FUTEX_WAKE:
            return futex_wake(uaddr, val);
            
        default:
            return -ENOSYS;
    }
}
