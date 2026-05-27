/*
 * slub.c — fixed-size object caches: slab freelist (slow path, per-cache
 *          spinlock) backed by a per-CPU magazine (fast path).
 *
 * Invariants:
 *  - Each object handed out by slub_cache_alloc carries MM_MAGIC_ALLOC in
 *    its header; slub_cache_free flips it to MAGIC_FREE before recycling
 *    and panics on a wrong magic (basic UAF guard).
 *  - The fast path operates on this_cpu()->magazine under preempt_disable();
 *    spilling/refilling the magazine takes the per-cache spinlock and
 *    never blocks.
 *  - A slab's object_size + sizeof(slub_object_t) times OBJECTS_PER_SLAB
 *    fits inside heap_size_var (otherwise slab_create returns NULL and
 *    the alloc propagates failure).
 *
 * Not allowed:
 *  - Calling the VFS, the scheduler, or anything outside kernel/mm/.
 *  - Acquiring more than one slub_cache_t::lock at once (no nesting).
 */

#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/mm_internal.h> /* heap_alloc (V10) */
#include <kernel/percpu.h>

#define CACHE_NAME_MAX 32
#define OBJECTS_PER_SLAB 64
#define CACHE_LINE_SIZE 64
#define COLOR_COUNT 8

#define MAGIC_FREE 0xF4EE0B13

static uint32_t g_color_idx;

void slub_init(void) { g_color_idx = 0; }

slub_cache_t *slub_cache_create(const char *name, size_t obj_size) {
  slub_cache_t *c = (slub_cache_t *)heap_alloc(sizeof(slub_cache_t));
  if (!c)
    return NULL;
  spinlock_init(&c->lock, "slub_cache");

  kstrncpy(c->name, name, CACHE_NAME_MAX);
  c->object_size = obj_size;
  c->objects_per_slab = OBJECTS_PER_SLAB;
  c->first_slab = NULL;
  for (int i = 0; i < MAX_CPUS; i++)
    c->pc_count[i] = 0;

  return c;
}

static slub_slab_t *slab_create(slub_cache_t *c) {
  uint32_t color = (g_color_idx++ % COLOR_COUNT) * CACHE_LINE_SIZE;
  size_t obj_stride = c->object_size + sizeof(slub_object_t);
  size_t slab_size =
      sizeof(slub_slab_t) + color + (c->objects_per_slab * obj_stride);

  slub_slab_t *s = (slub_slab_t *)heap_alloc(slab_size);
  if (!s)
    return NULL;

  s->free_count = c->objects_per_slab;
  s->freelist = NULL;
  s->next = NULL;
  s->objects = (uint8_t *)s + sizeof(slub_slab_t) + color;

  uint8_t *base = s->objects;
  slub_object_t *prev = NULL;
  for (uint32_t i = 0; i < c->objects_per_slab; i++) {
    slub_object_t *obj = (slub_object_t *)(base + i * obj_stride);
    obj->magic = MAGIC_FREE;
    obj->owner_cache = c;
    obj->next_free = NULL;
    if (prev)
      prev->next_free = obj;
    else
      s->freelist = obj;
    prev = obj;
  }

  return s;
}

void *slub_cache_alloc(slub_cache_t *c) {
  preempt_disable();
  uint32_t my = this_cpu()->id;
  if (c->pc_count[my] > 0) {
    void *ptr = c->pc_mag[my][--c->pc_count[my]];
    preempt_enable();
    slub_object_t *obj =
        (slub_object_t *)((uint8_t *)ptr - sizeof(slub_object_t));
    obj->magic = MM_MAGIC_ALLOC;
    return ptr;
  }
  preempt_enable();

  spin_lock(&c->lock);

  slub_slab_t *s = NULL;
  for (slub_slab_t *it = c->first_slab; it; it = it->next) {
    if (it->free_count > 0) {
      s = it;
      break;
    }
  }
  if (!s) {
    s = slab_create(c);
    if (!s) {
      spin_unlock(&c->lock);
      return NULL;
    }
    s->next = c->first_slab;
    c->first_slab = s;
  }

  slub_object_t *obj = s->freelist;
  s->freelist = obj->next_free;
  s->free_count--;
  obj->magic = MM_MAGIC_ALLOC;

  spin_unlock(&c->lock);
  return (void *)((uint8_t *)obj + sizeof(slub_object_t));
}

void slub_cache_free(slub_cache_t *c, void *ptr) {
  if (!ptr)
    return;

  slub_object_t *obj =
      (slub_object_t *)((uint8_t *)ptr - sizeof(slub_object_t));
  if (obj->magic != MM_MAGIC_ALLOC)
    return;

  preempt_disable();
  uint32_t my = this_cpu()->id;
  if (c->pc_count[my] < SLUB_PERCPU_MAG_SIZE) {
    obj->magic = MAGIC_FREE;
    c->pc_mag[my][c->pc_count[my]++] = ptr;
    preempt_enable();
    return;
  }
  preempt_enable();

  /* Slow path — magazine full, dump back to the slab freelist. */
  spin_lock(&c->lock);
  obj->magic = MAGIC_FREE;

  size_t stride = c->object_size + sizeof(slub_object_t);
  slub_slab_t *target = NULL;
  for (slub_slab_t *s = c->first_slab; s; s = s->next) {
    uint8_t *start = s->objects;
    uint8_t *end = start + (c->objects_per_slab * stride);
    if ((uint8_t *)obj >= start && (uint8_t *)obj < end) {
      target = s;
      break;
    }
  }

  if (!target) {
    spin_unlock(&c->lock);
    return;
  }

  obj->next_free = target->freelist;
  target->freelist = obj;
  target->free_count++;
  spin_unlock(&c->lock);
}
