#ifndef KERNEL_SCHED_ARCH_H
#define KERNEL_SCHED_ARCH_H

#include <kernel/process.h>
#include <stdint.h>

void asm_context_switch(process_context_t *old_ctx, process_context_t *new_ctx);
void jump_to_usermode(uint32_t entry, uint32_t stack);
void fork_child_return(void);

#endif
