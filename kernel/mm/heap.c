#include <stdint.h>
#include <stddef.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/heap_internal.h>
#include <kernel/memory_layout.h>
#include <kernel/spinlock.h>
#include <kernel/kprintf.h>

static uint32_t heap_base = KERNEL_HEAP_START;
static uint32_t heap_size_var = (KERNEL_HEAP_END - KERNEL_HEAP_START);
static uint32_t heap_end_var = KERNEL_HEAP_END;
static block_header_t* heap_start = NULL;
static uint32_t total_allocated = 0;
static uint32_t total_free = 0;

static spinlock_t heap_lock = SPINLOCK_INIT("heap");

void heap_init(void) {
    heap_start = (block_header_t*)heap_base;
    heap_start->size = heap_size_var;
    heap_start->is_free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    total_free = heap_size_var - sizeof(block_header_t);
    total_allocated = 0;
    
    kprintf("Heap: Initialized %dMB heap at 0x%x - 0x%x\n", 
            (int)(heap_size_var / (1024*1024)), heap_base, heap_end_var);
}

static void split_block(block_header_t* block, uint32_t size) {
    if (block->size > size + sizeof(block_header_t) + 16) {
        block_header_t* new_block = (block_header_t*)((uint8_t*)block + size);
        if ((uint32_t)new_block < heap_base || (uint32_t)new_block >= heap_end_var) {
            return;
        }
        new_block->size = block->size - size;
        new_block->is_free = 1;
        new_block->next = block->next;
        new_block->prev = block;
        
        if (block->next) {
            block->next->prev = new_block;
        }
        block->next = new_block;
        block->size = size;
    }
}

static void merge_free_blocks(block_header_t* block) {
    if (block->next && block->next->is_free) {
        block->size += block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }
    if (block->prev && block->prev->is_free) {
        block->prev->size += block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void* malloc(uint32_t size) {
    if (size == 0 || size > heap_size_var - sizeof(block_header_t) * 2) {
        return NULL;
    }
    size = (size + 3) & ~3;
    uint32_t total_size = size + sizeof(block_header_t);
    if (total_size > heap_size_var) return NULL;
    
    spin_lock(&heap_lock);
    
    block_header_t* current = heap_start;
    uint32_t largest_free = 0;
    int safety_counter = 4096;
    while (current && safety_counter-- > 0) {
        if ((uint32_t)current < heap_base || (uint32_t)current >= heap_end_var) break;
        if (current->is_free && current->size > largest_free) {
            largest_free = current->size;
        }
        if (current->is_free && current->size >= total_size) {
            current->is_free = 0;
            split_block(current, total_size);
            uint32_t data_size = current->size - sizeof(block_header_t);
            total_allocated += data_size;
            if (total_free >= data_size) total_free -= data_size;
            spin_unlock(&heap_lock);
            return (void*)((uint8_t*)current + sizeof(block_header_t));
        }
        current = current->next;
    }
    
    spin_unlock(&heap_lock);
    return NULL;
}

void free(void* ptr) {
    if (!ptr) return;
    block_header_t* block = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    if ((uint32_t)block < heap_base || (uint32_t)block >= heap_end_var) return;
    
    spin_lock(&heap_lock);
    
    if (block->is_free == 1) {
        spin_unlock(&heap_lock);
        return;
    }
    if (block->size == 0 || block->size > heap_size_var || block->size < sizeof(block_header_t)) {
        spin_unlock(&heap_lock);
        return;
    }
    if ((uint32_t)block + block->size > heap_end_var) {
        spin_unlock(&heap_lock);
        return;
    }
    
    block->is_free = 1;
    uint32_t data_size = block->size - sizeof(block_header_t);
    if (total_allocated >= data_size) {
        total_allocated -= data_size;
        total_free += data_size;
    }
    merge_free_blocks(block);
    
    spin_unlock(&heap_lock);
}

void heap_stats(uint32_t* allocated, uint32_t* free_mem) {
    if (allocated) *allocated = total_allocated;
    if (free_mem) *free_mem = total_free;
}
