#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <kernel/barriers.h>
#include <kernel/lockdep.h>
#include <kernel/preempt.h>
#include <stdint.h>

typedef struct spinlock {
  volatile uint32_t locked;
  const char *name;
  struct cpu *holder;
} spinlock_t;

#define SPINLOCK_INIT(name_str)                                                \
  {.locked = 0, .name = (name_str), .holder = (struct cpu *)0}

static inline void spinlock_init(spinlock_t *lock, const char *name) {
  lock->locked = 0;
  lock->name = name;
  lock->holder = (struct cpu *)0;
}

static inline void spin_lock(spinlock_t *lock) {
  __asm__ volatile("cli");
  preempt_disable();

  while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
    cpu_relax();
  }

  lock->holder = this_cpu();
  lockdep_acquire(lock->name);
}

static inline void spin_unlock(spinlock_t *lock) {
  lockdep_release(lock->name);
  lock->holder = (struct cpu *)0;

  __atomic_clear(&lock->locked, __ATOMIC_RELEASE);

  preempt_enable();
  __asm__ volatile("sti");
}

static inline int spin_trylock(spinlock_t *lock) {
  __asm__ volatile("cli");
  preempt_disable();

  if (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
    preempt_enable();
    __asm__ volatile("sti");
    return 0;
  }

  lock->holder = this_cpu();
  lockdep_acquire(lock->name);
  return 1;
}

static inline void raw_spin_lock(spinlock_t *lock) {
  preempt_disable();
  while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
    cpu_relax();
  }
  lock->holder = this_cpu();
  lockdep_acquire(lock->name);
}

static inline void raw_spin_unlock(spinlock_t *lock) {
  lockdep_release(lock->name);
  lock->holder = (struct cpu *)0;
  __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
  preempt_enable();
}

static inline int spin_is_locked(const spinlock_t *lock) {
  return __atomic_load_n(&lock->locked, __ATOMIC_RELAXED);
}

static inline int spin_held_by_me(const spinlock_t *lock) {
  return spin_is_locked(lock) && lock->holder == this_cpu();
}

static inline void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags) {
  __asm__ volatile("pushf\n\t"
                   "pop %0\n\t"
                   "cli"
                   : "=r"(*flags)::"memory");
  preempt_disable();

  while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
    cpu_relax();
  }

  lock->holder = this_cpu();
  lockdep_acquire(lock->name);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags) {
  lockdep_release(lock->name);
  lock->holder = (struct cpu *)0;

  __atomic_clear(&lock->locked, __ATOMIC_RELEASE);

  preempt_enable();
  __asm__ volatile("push %0; popf" ::"r"(flags) : "memory", "cc");
}

static inline uint32_t local_irq_save(void) {
  uint32_t flags;
  __asm__ volatile("pushf; pop %0; cli" : "=r"(flags)::"memory");
  return flags;
}

static inline void local_irq_restore(uint32_t flags) {
  __asm__ volatile("push %0; popf" ::"r"(flags) : "memory", "cc");
}

#endif
