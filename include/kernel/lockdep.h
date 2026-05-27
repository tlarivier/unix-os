#ifndef KERNEL_LOCKDEP_H
#define KERNEL_LOCKDEP_H

#include <stdint.h>

#ifdef CONFIG_LOCKDEP

void lockdep_acquire(const char *name);
void lockdep_release(const char *name);

#else /* !CONFIG_LOCKDEP */

static inline void lockdep_acquire(const char *name) { (void)name; }
static inline void lockdep_release(const char *name) { (void)name; }

#endif /* CONFIG_LOCKDEP */

#endif
