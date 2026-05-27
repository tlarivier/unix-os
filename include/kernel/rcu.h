#ifndef _KERNEL_RCU_H
#define _KERNEL_RCU_H

#include <stdint.h>

#include <kernel/percpu.h>
static inline void rcu_read_lock(void) { preempt_disable(); }

static inline void rcu_read_unlock(void) { preempt_enable(); }

#define rcu_dereference(p)                                                     \
  ({                                                                           \
    __typeof__(p) __rcu_v = (p);                                               \
    __sync_synchronize();                                                      \
    __rcu_v;                                                                   \
  })

#define rcu_assign_pointer(p, v)                                               \
  do {                                                                         \
    __sync_synchronize();                                                      \
    (p) = (v);                                                                 \
  } while (0)

void synchronize_rcu(void);

#endif /* _KERNEL_RCU_H */
