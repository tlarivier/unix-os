#ifndef KERNEL_PREEMPT_H
#define KERNEL_PREEMPT_H

#include <stdint.h>

struct cpu;

#define MAX_CPUS 8

extern uint32_t cpu_count;
static inline struct cpu *this_cpu(void) {
  struct cpu *p;
  __asm__ volatile("mov %%gs:0, %0" : "=r"(p));
  return p;
}

void preempt_disable(void);
void preempt_enable(void);
int preempt_count(void);

#endif
