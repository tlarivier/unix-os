#ifndef _KERNEL_RANDOM_H
#define _KERNEL_RANDOM_H

#include <stdint.h>

extern _Atomic uint32_t __random_state;

void random_init(void);
uint32_t random_u32(void);

static inline uint32_t random32(void) {
  uint32_t old = __atomic_load_n(&__random_state, __ATOMIC_RELAXED);
  uint32_t next;
  do {
    uint32_t x = old;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    next = x;
  } while (!__atomic_compare_exchange_n(&__random_state, &old, next, 1,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED));
  return next;
}

static inline uint32_t random_range(uint32_t max) {
  if (max == 0)
    return 0;
  return random32() % max;
}

#endif
