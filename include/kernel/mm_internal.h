#ifndef KERNEL_MM_INTERNAL_H
#define KERNEL_MM_INTERNAL_H

#include <kernel/paging.h>
#include <stddef.h>
#include <stdint.h>

uint32_t get_physical_addr_current(uint32_t virt);
void *temp_map_frame(uint32_t phys_frame);
void temp_unmap_frame(void);
void frame_incref(uint32_t addr);
void frame_decref(uint32_t addr);
uint16_t frame_getref(uint32_t addr);
void frame_alloc_reserve_low(uint32_t end_phys);
void frame_alloc_mark_used(uint32_t addr);

#include <kernel/memory_layout.h>
extern uint32_t *kernel_page_tables[MAX_PAGE_TABLES];

void *heap_alloc(uint32_t size);
void heap_free(void *ptr);
int handle_cow_fault(page_directory_t *dir, uint32_t fault_addr);
int handle_demand_fault(uint32_t fault_addr);

#define MM_MAGIC_ALLOC 0xA110CA7E

#endif /* KERNEL_MM_INTERNAL_H */
