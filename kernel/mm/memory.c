#include <stdint.h>
#include <stdbool.h>
#include <kernel/memory.h>
#include <kernel/kernel.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/heap_internal.h>

static slub_cache_t* kmalloc_8_cache = NULL;
static slub_cache_t* kmalloc_16_cache = NULL;
static slub_cache_t* kmalloc_32_cache = NULL;
static slub_cache_t* kmalloc_64_cache = NULL;
static slub_cache_t* kmalloc_128_cache = NULL;
static slub_cache_t* kmalloc_256_cache = NULL;
static slub_cache_t* kmalloc_512_cache = NULL;
static slub_cache_t* kmalloc_1024_cache = NULL;
static slub_cache_t* kmalloc_2048_cache = NULL;
static slub_cache_t* kmalloc_4096_cache = NULL;
static slub_cache_t* kmalloc_8192_cache = NULL;
static slub_cache_t* kmalloc_16384_cache = NULL;

static int memory_initialized = 0;

static slub_cache_t* get_cache_for_size(size_t size) {
    if (size <= 8)     return kmalloc_8_cache;
    if (size <= 16)    return kmalloc_16_cache;
    if (size <= 32)    return kmalloc_32_cache;
    if (size <= 64)    return kmalloc_64_cache;
    if (size <= 128)   return kmalloc_128_cache;
    if (size <= 256)   return kmalloc_256_cache;
    if (size <= 512)   return kmalloc_512_cache;
    if (size <= 1024)  return kmalloc_1024_cache;
    if (size <= 2048)  return kmalloc_2048_cache;
    if (size <= 4096)  return kmalloc_4096_cache;
    if (size <= 8192)  return kmalloc_8192_cache;
    if (size <= 16384) return kmalloc_16384_cache;
    return NULL;
}

void memory_init(void) {
    heap_init();
    slub_init();
    kmalloc_8_cache     = slub_cache_create("kmalloc-8",     8);
    kmalloc_16_cache    = slub_cache_create("kmalloc-16",    16);
    kmalloc_32_cache    = slub_cache_create("kmalloc-32",    32);
    kmalloc_64_cache    = slub_cache_create("kmalloc-64",    64);
    kmalloc_128_cache   = slub_cache_create("kmalloc-128",   128);
    kmalloc_256_cache   = slub_cache_create("kmalloc-256",   256);
    kmalloc_512_cache   = slub_cache_create("kmalloc-512",   512);
    kmalloc_1024_cache  = slub_cache_create("kmalloc-1024",  1024);
    kmalloc_2048_cache  = slub_cache_create("kmalloc-2048",  2048);
    kmalloc_4096_cache  = slub_cache_create("kmalloc-4096",  4096);
    kmalloc_8192_cache  = slub_cache_create("kmalloc-8192",  8192);
    kmalloc_16384_cache = slub_cache_create("kmalloc-16384", 16384);
    
    memory_initialized = 1;
}

void* kmalloc(size_t size) {
    if (!memory_initialized) return malloc((uint32_t)size);
    if (size == 0) return NULL;
    
    slub_cache_t* cache = get_cache_for_size(size);
    if (cache) {
        void* ptr = slub_alloc(cache);
        if (ptr) return ptr;
    }
    return malloc((uint32_t)size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    if (!memory_initialized) { free(ptr); return; }
    
    slub_object_t* obj = (slub_object_t*)((uint8_t*)ptr - sizeof(slub_object_t));
    if (obj->magic == 0x12345678 && obj->owner_cache != NULL) {
        slub_free(obj->owner_cache, ptr);
        return;
    }
    free(ptr);
}

void* kcalloc(size_t count, size_t size) {
    if (count == 0 || size == 0) return NULL;
    size_t total_size = count * size;
    if (total_size / count != size) return NULL;
    void* ptr = kmalloc(total_size);
    if (!ptr) return NULL;
    memset(ptr, 0, total_size);
    return ptr;
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }
    
    size_t old_size = new_size;  /* Fallback: assume same size */
    slub_object_t* obj = (slub_object_t*)((uint8_t*)ptr - sizeof(slub_object_t));
    if (obj->magic == 0x12345678 && obj->owner_cache) {
        old_size = obj->owner_cache->object_size;
    } else {
        block_header_t* hdr = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
        if (hdr->size > sizeof(block_header_t) && hdr->size < HEAP_MAX_SIZE) {
            old_size = hdr->size - sizeof(block_header_t);
        }
    }
    
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);
    return new_ptr;
}

void memory_stats(void) {
    if (!memory_initialized) return;
    uint32_t heap_alloc, heap_free;
    heap_stats(&heap_alloc, &heap_free);
    kprintf("Memory: heap=%u/%u\n", heap_alloc, heap_free);
}
