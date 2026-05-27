/*
 * heap.c — kernel heap allocator: first-fit doubly-linked free list with
 *          immediate split + coalesce, KASAN canaries, single global lock.
 *
 * Invariants:
 *  - The arena spans [KERNEL_HEAP_START, KERNEL_HEAP_END) and never overlaps
 *    any frame returned by allocate_frame().
 *  - Every operation on the free list holds heap_lock; block headers are
 *    contiguous, doubly-linked, and adjacent free blocks are merged before
 *    the lock is released.
 *  - KASAN canaries and poison are written for every alloc/free; a caller
 *    that frees a corrupted block triggers kasan_free_check before the
 *    block is recycled.
 *
 * Not allowed:
 *  - Being called from outside kernel/mm/ (heap_alloc/heap_free are
 *    MM-internal; feature code goes through kmalloc/kfree).
 *  - Calling the VFS, the scheduler, or any user-pointer helper.
 */

#include <kernel/kasan.h>
#include <kernel/kprintf.h>
#include <kernel/memory.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/spinlock.h>
#include <stddef.h>
#include <stdint.h>

typedef struct block_header {
  uint32_t size;
  uint32_t is_free;
  struct block_header *next;
  struct block_header *prev;
} block_header_t;

static uint32_t heap_base = KERNEL_HEAP_START;
static uint32_t heap_size_var = (KERNEL_HEAP_END - KERNEL_HEAP_START);
static uint32_t heap_end_var = KERNEL_HEAP_END;
static block_header_t *heap_start = NULL;

static spinlock_t heap_lock = SPINLOCK_INIT("heap");

void heap_init(void) {
  heap_start = (block_header_t *)heap_base;
  heap_start->size = heap_size_var;
  heap_start->is_free = 1;
  heap_start->next = NULL;
  heap_start->prev = NULL;

  kasan_poison_range((uint8_t *)heap_base + sizeof(block_header_t),
                     heap_size_var - sizeof(block_header_t));

  kprintf("Heap: Initialized %dMB heap at 0x%x - 0x%x\n",
          (int)(heap_size_var / (1024 * 1024)), heap_base, heap_end_var);
}

static void split_block(block_header_t *block, uint32_t size) {
  if (block->size > size + sizeof(block_header_t) + 16) {
    block_header_t *new_block = (block_header_t *)((uint8_t *)block + size);
    if ((uint32_t)new_block < heap_base ||
        (uint32_t)new_block >= heap_end_var) {
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

static void merge_free_blocks(block_header_t *block) {
  if (block->next && block->next->is_free) {
    block_header_t *next = block->next;
    uint32_t next_size = next->size;
    block_header_t *nn = next->next;
    kasan_poison_range(next, sizeof(block_header_t));
    block->size += next_size;
    block->next = nn;
    if (nn)
      nn->prev = block;
  }
  if (block->prev && block->prev->is_free) {
    block_header_t *prev = block->prev;
    block_header_t *nn = block->next;
    uint32_t this_size = block->size;
    kasan_poison_range(block, sizeof(block_header_t));
    prev->size += this_size;
    prev->next = nn;
    if (nn)
      nn->prev = prev;
  }
}

void *heap_alloc(uint32_t size) {
  if (size == 0 || size > heap_size_var - sizeof(block_header_t) * 2) {
    return NULL;
  }
  uint32_t raw_size = (uint32_t)kasan_raw_size(size);
  raw_size = (raw_size + 3) & ~3;
  uint32_t total_size = raw_size + sizeof(block_header_t);
  if (total_size > heap_size_var)
    return NULL;

  spin_lock(&heap_lock);

  block_header_t *blk = heap_start;
  while (blk) {
    if (blk->is_free && blk->size >= total_size) {
      blk->is_free = 0;
      split_block(blk, total_size);
      uint32_t data_size = blk->size - sizeof(block_header_t);
      spin_unlock(&heap_lock);

      void *raw_payload = (uint8_t *)blk + sizeof(block_header_t);
      kasan_check_poison(raw_payload, data_size);
      return kasan_alloc_register(raw_payload, data_size, size);
    }
    blk = blk->next;
  }

  spin_unlock(&heap_lock);
  return NULL;
}

void heap_free(void *ptr) {
  if (!ptr)
    return;
#ifdef CONFIG_KASAN_LITE
  uint32_t *raw_head = (uint32_t *)((uint8_t *)ptr - KASAN_CANARY_BYTES);
  if (raw_head[0] == KASAN_POISON_WORD) {
    kasan_check_poison(raw_head, KASAN_CANARY_BYTES);
  }
#endif
  void *raw_payload = kasan_free_check(ptr, NULL);

  block_header_t *block =
      (block_header_t *)((uint8_t *)raw_payload - sizeof(block_header_t));
  if ((uint32_t)block < heap_base || (uint32_t)block >= heap_end_var)
    return;

  spin_lock(&heap_lock);

  if (block->is_free == 1) {
    spin_unlock(&heap_lock);
    return;
  }
  if (block->size == 0 || block->size > heap_size_var ||
      block->size < sizeof(block_header_t)) {
    spin_unlock(&heap_lock);
    return;
  }
  if ((uint32_t)block + block->size > heap_end_var) {
    spin_unlock(&heap_lock);
    return;
  }

  block->is_free = 1;
  merge_free_blocks(block);

  spin_unlock(&heap_lock);
}
