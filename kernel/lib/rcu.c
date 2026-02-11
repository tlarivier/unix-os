#include <kernel/rcu.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>

static struct rcu_head *rcu_callbacks = NULL;
static spinlock_t rcu_lock = SPINLOCK_INIT("rcu");

static volatile uint32_t rcu_grace_period = 0;

void synchronize_rcu(void) {
    extern void schedule(void);
    
    __sync_add_and_fetch(&rcu_grace_period, 1);
    
    schedule();
    
    /* After returning, all pre-existing readers have finished */
}

void call_rcu(struct rcu_head *head, rcu_callback_t func, void *arg) {
    head->func = func;
    head->arg = arg;
    
    spin_lock(&rcu_lock);
    head->next = rcu_callbacks;
    rcu_callbacks = head;
    spin_unlock(&rcu_lock);
}

void rcu_process_callbacks(void) {
    struct rcu_head *list;
    
    spin_lock(&rcu_lock);
    list = rcu_callbacks;
    rcu_callbacks = NULL;
    spin_unlock(&rcu_lock);
    
    /* Process all pending callbacks */
    while (list) {
        struct rcu_head *next = list->next;
        if (list->func) {
            list->func(list->arg);
        }
        list = next;
    }
}
