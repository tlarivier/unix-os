#ifndef KERNEL_PERCPU_H
#define KERNEL_PERCPU_H

#include <kernel/preempt.h>
#include <kernel/spinlock.h>
#include <stdint.h>

#define FRAME_MAGAZINE_SIZE 16

struct process;

typedef struct cpu {
  struct cpu *self;
  uint32_t id;
  uint32_t lapic_id;
  struct process *current_proc;
  int preempt_count;
  int intr_depth;
  void *kernel_stack;
  void *run_queues[4];
  uint32_t sched_ticks;
  uint32_t schedule_calls;
  uint32_t last_steal_tick;
  uint32_t quantum_left;
  int current_queue;
  volatile int need_resched;
  void *idle_proc;
  spinlock_t rq_lock;
  void *syscall_regs;
  int lockdep_stack[32];
  int lockdep_depth;
  volatile uint32_t rcu_qs_count;
  uint32_t frame_magazine[FRAME_MAGAZINE_SIZE];
  uint32_t frame_magazine_count;
  /* TODO §G.2: move this to kernel/fs/ext2.c so percpu.h doesn't
   * have to know about block I/O. */
  uint8_t block_buf[4096] __attribute__((aligned(4096)));
} cpu_t;

extern cpu_t cpus[MAX_CPUS];
void percpu_init_bsp(void);
void percpu_init_ap(uint32_t cpu_id);
#define current_process (this_cpu()->current_proc)

#endif
