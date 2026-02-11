#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/process.h>

void scheduler_init(void);
void scheduler_enable(void);

void scheduler_add_process(process_t* proc);
void scheduler_remove_process(process_t* proc);

void schedule(void);
void yield(void);
void block_process(void);
void wake_process(process_t* proc);

void timer_tick(void);

uint32_t get_context_switches(void);
void print_scheduler_stats(void);

#endif
