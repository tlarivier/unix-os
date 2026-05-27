#ifndef KERNEL_PROCESS_MM_H
#define KERNEL_PROCESS_MM_H

#include <kernel/paging.h>
#include <stdint.h>

process_memory_t *create_process_memory(void);
process_memory_t *clone_process_memory(process_memory_t *src);
void destroy_process_memory(process_memory_t *mem);

#endif /* KERNEL_PROCESS_MM_H */
