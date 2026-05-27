#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <kernel/constants.h>
#include <stddef.h>
#include <stdint.h>

typedef struct page_table page_table_t;

typedef struct page_directory {
  uint32_t tables_physical[1024];
  uint32_t physical_addr;
} page_directory_t;

typedef struct page_table {
  uint32_t pages[1024];
} page_table_t;

#define MAX_MMAP_REGIONS 64
typedef struct mmap_region {
  uint32_t addr;
  uint32_t len;
  uint32_t prot;
  uint32_t flags;
  int in_use;
} mmap_region_t;

typedef struct process_memory {
  page_directory_t *page_directory;
  uint32_t brk;
  uint32_t stack_base;
  mmap_region_t mmap_regions[MAX_MMAP_REGIONS];
  uint32_t mmap_next_addr;
} process_memory_t;

void paging_init(void);
page_directory_t *create_page_directory(void);
void destroy_page_directory(page_directory_t *dir);
void switch_page_directory(page_directory_t *dir);

int map_page(page_directory_t *dir, uint32_t virtual_addr,
             uint32_t physical_addr, uint32_t flags);
void unmap_page(page_directory_t *dir, uint32_t virtual_addr);

uint32_t get_physical_addr(page_directory_t *dir, uint32_t virtual_addr);
uint32_t allocate_frame(void);
void free_frame(uint32_t frame_addr);
void zero_frame(uint32_t frame_addr);
void *copy_to_frame(uint32_t frame, uint32_t offset, const void *src,
                    size_t len);
int mmap_file_register(uint32_t start, uint32_t end, int fd, uint32_t offset,
                       uint32_t prot);
void mmap_file_unregister(uint32_t start, uint32_t end);
void mmap_file_clear_pd(uint32_t pd_phys);
extern void set_cr3(uint32_t page_directory_physical);
extern uint32_t kernel_page_directory[1024];
int map_uncached_mmio(uint32_t phys);

#endif // KERNEL_PAGING_H
