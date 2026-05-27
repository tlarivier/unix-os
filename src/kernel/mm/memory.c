/*
 * memory.c — public kernel allocator facade: memory_init wires heap + 12
 *            SLUB buckets; kmalloc/kfree route by size and fall back to
 *            the raw heap.
 *
 * Invariants:
 *  - memory_init runs exactly once at boot, before any kmalloc/kfree call;
 *    memory_initialized gates early callers.
 *  - kmalloc(sz) goes to the smallest kmalloc bucket whose object_size >= sz
 *    and >= 16384 bytes spills to heap_alloc; the matching kfree dispatches
 *    via the slub_object_t magic + owner_cache.
 *  - kfree(NULL) is a no-op; kfree of a pointer outside
 *    [KERNEL_HEAP_START, KERNEL_HEAP_END) is rejected before any header is
 *    dereferenced (V19).
 *
 * Not allowed:
 *  - Calling the VFS, the scheduler, or any user-pointer helper.
 *  - Bypassing kmalloc/kfree to reach heap_alloc/heap_free or slub_cache_*
 *    from outside kernel/mm/.
 */

#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <stdbool.h>
#include <stdint.h>

#define NUM_KMALLOC_CACHES 12
static const size_t kmalloc_sizes[NUM_KMALLOC_CACHES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
static const char *kmalloc_names[NUM_KMALLOC_CACHES] = {
    "kmalloc-8",    "kmalloc-16",   "kmalloc-32",   "kmalloc-64",
    "kmalloc-128",  "kmalloc-256",  "kmalloc-512",  "kmalloc-1024",
    "kmalloc-2048", "kmalloc-4096", "kmalloc-8192", "kmalloc-16384"};
static slub_cache_t *kmalloc_caches[NUM_KMALLOC_CACHES];

static int memory_initialized = 0;

static slub_cache_t *get_cache_for_size(size_t size) {
  for (int i = 0; i < NUM_KMALLOC_CACHES; i++) {
    if (size <= kmalloc_sizes[i])
      return kmalloc_caches[i];
  }
  return NULL;
}

void memory_init(void) {
  heap_init();
  slub_init();
  for (int i = 0; i < NUM_KMALLOC_CACHES; i++) {
    kmalloc_caches[i] = slub_cache_create(kmalloc_names[i], kmalloc_sizes[i]);
  }

  memory_initialized = 1;
}

void *kmalloc(size_t size) {
  if (!memory_initialized)
    return heap_alloc((uint32_t)size);
  if (size == 0)
    return NULL;

  slub_cache_t *cache = get_cache_for_size(size);
  if (cache) {
    void *ptr = slub_cache_alloc(cache);
    if (ptr)
      return ptr;
  }
  return heap_alloc((uint32_t)size);
}

void kfree(void *ptr) {
  if (!ptr)
    return;
  if (!memory_initialized) {
    heap_free(ptr);
    return;
  }

  uintptr_t p = (uintptr_t)ptr;
  if (p < (uintptr_t)KERNEL_HEAP_START + sizeof(slub_object_t) ||
      p >= (uintptr_t)KERNEL_HEAP_END) {
    return;
  }

  slub_object_t *obj =
      (slub_object_t *)((uint8_t *)ptr - sizeof(slub_object_t));
  if (obj->magic == MM_MAGIC_ALLOC && obj->owner_cache != NULL) {
    slub_cache_free(obj->owner_cache, ptr);
    return;
  }
  heap_free(ptr);
}
