#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/constants.h>

// Paging implementation constants
#define PAGE_ENTRIES     1024
#define PAGES_PER_TABLE  1024
#define PAGES_PER_DIR    1024

// All page constants now in constants.h - no redefinitions

// Virtual memory layout - now in constants.h
// USER_STACK_BASE is defined in constants.h

// Forward declaration
typedef struct page_table page_table_t;

// Page directory structure
typedef struct page_directory {
    uint32_t tables_physical[1024];     // Physical addresses of page tables
    page_table_t* tables_virtual[1024]; // Virtual addresses for accessing tables
    uint32_t physical_addr;             // Physical address of tables_physical
} page_directory_t;

// Page table structure
typedef struct page_table {
    uint32_t pages[1024];
} page_table_t;

// Virtual memory region structure
typedef struct vm_area {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
    struct vm_area* next;
} vm_area_t;

// VMA flags for demand paging
#define VMA_READ    0x01
#define VMA_WRITE   0x02
#define VMA_EXEC    0x04
#define VMA_LAZY    0x08   /* Pages not yet allocated - demand paging */
#define VMA_STACK   0x10   /* Stack region - grows down */
#define VMA_HEAP    0x20   /* Heap region - grows up */

// mmap region tracking (per-process)
#define MAX_MMAP_REGIONS 64
typedef struct mmap_region {
    uint32_t addr;
    uint32_t len;
    uint32_t prot;
    uint32_t flags;
    int in_use;
} mmap_region_t;

// Process memory management
typedef struct process_memory {
    page_directory_t* page_directory;
    vm_area_t* vm_areas;
    uint32_t brk;                    // Process heap break
    uint32_t stack_base;             // User stack base
    mmap_region_t mmap_regions[MAX_MMAP_REGIONS];  // Per-process mmap regions
    uint32_t mmap_next_addr;         // Next mmap allocation address
    int refcount;                    // Reference count for CLONE_VM sharing
} process_memory_t;

// Paging functions
void paging_init(void);
page_directory_t* create_page_directory(void);
void destroy_page_directory(page_directory_t* dir);
void switch_page_directory(page_directory_t* dir);
int map_page_ext(page_directory_t* dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
void unmap_page_ext(page_directory_t* dir, uint32_t virtual_addr);
uint32_t get_physical_addr(page_directory_t* dir, uint32_t virtual_addr);
uint32_t allocate_frame(void);
void free_frame(uint32_t frame_addr);
void zero_frame(uint32_t frame_addr);
void* copy_to_frame(uint32_t frame, uint32_t offset, const void* src, size_t len);

// Process memory management
process_memory_t* create_process_memory(void);
process_memory_t* clone_process_memory(process_memory_t* src);
void destroy_process_memory(process_memory_t* mem);
int map_user_pages(process_memory_t* mem, uint32_t virtual_base, uint32_t size, uint32_t flags);
void setup_user_stack(process_memory_t* mem);

// COW (Copy-on-Write) support
int handle_cow_fault(uint32_t fault_addr);
void frame_incref(uint32_t frame_addr);
uint8_t frame_getref(uint32_t frame_addr);

// Demand paging support
int handle_demand_fault(uint32_t fault_addr);
vm_area_t* vma_find(process_memory_t* mem, uint32_t addr);
int vma_add(process_memory_t* mem, uint32_t start, uint32_t end, uint32_t flags);
int map_user_pages_lazy(process_memory_t* mem, uint32_t virtual_base, uint32_t size, uint32_t flags);

// Temporary mapping for accessing high memory frames
void* temp_map_frame(uint32_t frame_addr);
void temp_unmap_frame(void);

// Sync page directory to physical frame (must call after map_page_ext)
void sync_page_directory(page_directory_t* dir);

// Assembly functions
extern void enable_paging(uint32_t page_directory_physical);
extern uint32_t get_cr3(void);
extern void set_cr3(uint32_t page_directory_physical);

// Global page directory
extern page_directory_t* kernel_directory;
extern page_directory_t* current_directory;

#endif // PAGING_H
