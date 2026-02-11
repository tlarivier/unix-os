#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/constants.h>

void* malloc(uint32_t size);
void free(void* ptr);

#define kheap_alloc(sz) malloc((uint32_t)(sz))
#define kheap_free(ptr) free((void*)(ptr))

#define PAGE_MASK          (PAGE_SIZE - 1)
#define PAGE_ALIGN(addr)   (((addr) + PAGE_MASK) & ~PAGE_MASK)

#define FRAMES_PER_BUCKET   32
#define MAX_BUCKETS         1024

#define MEMORY_REGION_KERNEL    0x01
#define MEMORY_REGION_USER      0x02
#define MEMORY_REGION_DMA       0x04
#define MEMORY_REGION_IO        0x08

void memory_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* ptr, size_t new_size);

void heap_init(void);
void heap_stats(uint32_t* allocated, uint32_t* free_mem);

typedef struct slub_cache slub_cache_t;

typedef struct slub_object {
    uint32_t magic;
    struct slub_object* next_free;
    slub_cache_t* owner_cache;
} slub_object_t;

typedef struct slub_slab {
    void* objects;
    void* freelist;
    uint32_t free_count;
    struct slub_slab* next;
} slub_slab_t;

struct slub_cache {
    char name[32];
    size_t object_size;
    size_t objects_per_slab;
    slub_slab_t* first_slab;
    uint32_t total_objects;
    uint32_t free_objects;
    uint32_t allocations;
    uint32_t deallocations;
    struct slub_cache* next;
    uint32_t lock;
};

void slub_init(void);
slub_cache_t* slub_cache_create(const char* name, size_t size);
void slub_cache_destroy(slub_cache_t* cache);
void* slub_alloc(slub_cache_t* cache);
void slub_free(slub_cache_t* cache, void* ptr);

void paging_init(void);
uint32_t* get_current_page_directory(void);

void memory_protection_init(void);
void* protected_kmalloc(size_t size, const char* file, uint32_t line);
void protected_kfree(void* ptr, const char* file, uint32_t line);
int32_t validate_memory_access(void* ptr, size_t size, uint32_t flags);
void memory_leak_check(void);
int32_t validate_kernel_string(const char* str, size_t max_len);
int32_t setup_stack_guard(void* stack_base, size_t stack_size);
void test_memory_protection(void);

#define PROTECTED_KMALLOC(size) protected_kmalloc(size, __FILE__, __LINE__)
#define PROTECTED_KFREE(ptr) protected_kfree(ptr, __FILE__, __LINE__)

#endif
