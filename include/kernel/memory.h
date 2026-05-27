#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <kernel/constants.h>
#include <kernel/preempt.h>
#include <kernel/spinlock.h>
#include <stddef.h>
#include <stdint.h>

#define FRAMES_PER_BUCKET 32
#define MAX_BUCKETS 1024

#define MEMORY_REGION_KERNEL 0x01
#define MEMORY_REGION_USER 0x02
#define MEMORY_REGION_DMA 0x04
#define MEMORY_REGION_IO 0x08

void memory_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

void heap_init(void);

typedef struct slub_cache slub_cache_t;

typedef struct slub_object {
  uint32_t magic;
  struct slub_object *next_free;
  slub_cache_t *owner_cache;
} slub_object_t;

typedef struct slub_slab {
  uint8_t *objects;
  slub_object_t *freelist;
  uint32_t free_count;
  struct slub_slab *next;
} slub_slab_t;

#define SLUB_PERCPU_MAG_SIZE 8

struct slub_cache {
  char name[32];
  size_t object_size;
  size_t objects_per_slab;
  slub_slab_t *first_slab;
  spinlock_t lock;
  void *pc_mag[MAX_CPUS][SLUB_PERCPU_MAG_SIZE];
  uint32_t pc_count[MAX_CPUS];
};

void slub_init(void);
slub_cache_t *slub_cache_create(const char *name, size_t size);
void *slub_cache_alloc(slub_cache_t *cache);
void slub_cache_free(slub_cache_t *cache, void *ptr);

#endif
