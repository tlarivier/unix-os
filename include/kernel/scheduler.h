#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <kernel/process.h>
#include <stdint.h>

void scheduler_enable(void);

void scheduler_add_process(process_t *proc);
void scheduler_remove_process(process_t *proc);
void scheduler_register_idle(uint32_t cpu_id, process_t *idle);
void schedule(void);
void timer_tick(void);
void context_switch(process_t *old, process_t *new);

#endif
