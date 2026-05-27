/*
 * frame_alloc.c — physical 4 KiB frame allocator (bitmap + per-CPU magazine +
 * refcount).
 *
 * Invariants:
 *  - Every frame returned by allocate_frame() is >= KERNEL_HEAP_END; the
 *    sub-heap range is pre-reserved in the bitmap by paging_init so it
 *    never overlaps kmalloc's arena.
 *  - A live frame has frame_refcount[fidx] >= 1 for as long as any PTE
 *    points at it; it returns to the pool only when the count reaches 0.
 *  - The slow path takes frame_lock; the fast path operates on
 *    this_cpu()->magazine under preempt_disable() and never sleeps.
 *
 * Not allowed:
 *  - Calling the VFS, the scheduler, or process_* helpers.
 *  - Handing out frames below FRAME_ALLOC_FIRST_BITMAP_IDX (kernel image,
 *    page tables, heap).
 */

#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>

#define MAX_PHYS_MEM 0x04000000 /* 64MB */
#define FRAME_COUNT (MAX_PHYS_MEM / PAGE_SIZE_CONST)
#define BITMAP_SIZE (FRAME_COUNT / 32)

#define KERNEL_RESERVED_END KERNEL_HEAP_END

#define FRAME_ALLOC_FIRST_BITMAP_IDX (KERNEL_HEAP_END / PAGE_SIZE_CONST / 32)

_Static_assert(PAGE_SIZE_CONST == 4096, "x86 page size must be 4 KiB");
_Static_assert((MAX_PHYS_MEM % PAGE_SIZE_CONST) == 0,
               "max phys mem page-aligned");

static spinlock_t frame_lock = SPINLOCK_INIT("frame");
static uint32_t frame_bitmap[BITMAP_SIZE];
static uint32_t next_free = 0;

static uint16_t frame_refcount[FRAME_COUNT];

void frame_alloc_reserve_low(uint32_t end_phys) {
  for (uint32_t a = 0; a < end_phys; a += PAGE_SIZE_CONST) {
    uint32_t f = a / PAGE_SIZE_CONST;
    frame_bitmap[f / 32] |= (1 << (f % 32));
  }
}

void frame_alloc_mark_used(uint32_t addr) {
  uint32_t f = addr / PAGE_SIZE_CONST;
  frame_bitmap[f / 32] |= (1 << (f % 32));
}

static void frame_clear(uint32_t addr) {
  uint32_t f = addr / PAGE_SIZE_CONST;
  frame_bitmap[f / 32] &= ~(1 << (f % 32));
}

static uint32_t alloc_frames_locked(uint32_t *out, uint32_t want) {
  uint32_t got = 0;
  uint32_t start = (next_free < FRAME_ALLOC_FIRST_BITMAP_IDX)
                       ? FRAME_ALLOC_FIRST_BITMAP_IDX
                       : next_free;
  uint32_t end_idx = IDENTITY_MAP_END / PAGE_SIZE_CONST / 32;
  if (end_idx > BITMAP_SIZE)
    end_idx = BITMAP_SIZE;
  for (uint32_t i = start; i < end_idx && got < want; i++) {
    if (frame_bitmap[i] == 0xFFFFFFFF)
      continue;
    for (int j = 0; j < 32 && got < want; j++) {
      if (!(frame_bitmap[i] & (1 << j))) {
        uint32_t addr = (i * 32 + j) * PAGE_SIZE_CONST;
        if (addr >= IDENTITY_MAP_END)
          goto done;
        frame_alloc_mark_used(addr);
        next_free = i;
        uint32_t fidx = addr / PAGE_SIZE_CONST;
        if (fidx < FRAME_COUNT)
          frame_refcount[fidx] = 1;
        out[got++] = addr;
      }
    }
  }
done:
  return got;
}

uint32_t allocate_frame(void) {
  preempt_disable();
  cpu_t *me = this_cpu();
  if (me->frame_magazine_count > 0) {
    uint32_t addr = me->frame_magazine[--me->frame_magazine_count];
    uint32_t fidx = addr / PAGE_SIZE_CONST;
    if (fidx < FRAME_COUNT)
      frame_refcount[fidx] = 1;
    preempt_enable();
    return addr;
  }
  uint32_t batch[FRAME_MAGAZINE_SIZE];
  spin_lock(&frame_lock);
  uint32_t got = alloc_frames_locked(batch, FRAME_MAGAZINE_SIZE);
  spin_unlock(&frame_lock);
  if (got == 0) {
    preempt_enable();
    return (uint32_t)-1;
  }
  uint32_t addr = batch[0];
  for (uint32_t i = 1; i < got; i++) {
    me->frame_magazine[me->frame_magazine_count++] = batch[i];
  }
  preempt_enable();
  return addr;
}

static void release_frame_to_pool(uint32_t addr) {
  preempt_disable();
  cpu_t *me = this_cpu();
  if (me->frame_magazine_count < FRAME_MAGAZINE_SIZE) {
    me->frame_magazine[me->frame_magazine_count++] = addr;
    preempt_enable();
    return;
  }
  spin_lock(&frame_lock);
  for (int k = 0; k < (FRAME_MAGAZINE_SIZE / 2); k++) {
    uint32_t a = me->frame_magazine[--me->frame_magazine_count];
    frame_clear(a);
    uint32_t fi = (a / PAGE_SIZE_CONST) / 32;
    if (fi < next_free && fi >= FRAME_ALLOC_FIRST_BITMAP_IDX)
      next_free = fi;
  }
  frame_clear(addr);
  uint32_t i = (addr / PAGE_SIZE_CONST) / 32;
  if (i < next_free && i >= FRAME_ALLOC_FIRST_BITMAP_IDX)
    next_free = i;
  spin_unlock(&frame_lock);
  preempt_enable();
}

void free_frame(uint32_t addr) {
  if (addr == 0 || addr == (uint32_t)-1)
    return;
  if (addr < KERNEL_RESERVED_END)
    return;
  frame_decref(addr);
}

void frame_incref(uint32_t addr) {
  uint32_t fidx = addr / PAGE_SIZE_CONST;
  if (fidx >= FRAME_COUNT)
    return;
  spin_lock(&frame_lock);
  if (frame_refcount[fidx] < 0xFFFF)
    frame_refcount[fidx]++;
  spin_unlock(&frame_lock);
}

/* Locked snapshot — the value can change after we return, but readers that
 * hold a lock guaranteeing the frame is not concurrently incref'd (e.g. the
 * COW handler under cow_lock with refcount==1 ⇒ sole PTE) can act on it. */
uint16_t frame_getref(uint32_t addr) {
  uint32_t fidx = addr / PAGE_SIZE_CONST;
  if (fidx >= FRAME_COUNT)
    return 0;
  spin_lock(&frame_lock);
  uint16_t r = frame_refcount[fidx];
  spin_unlock(&frame_lock);
  return r;
}

void frame_decref(uint32_t addr) {
  uint32_t fidx = addr / PAGE_SIZE_CONST;
  if (fidx >= FRAME_COUNT)
    return;
  if (addr < KERNEL_RESERVED_END)
    return;
  spin_lock(&frame_lock);
  uint16_t r = frame_refcount[fidx];
  if (r == 0) {
    spin_unlock(&frame_lock);
    return;
  }
  if (r > 1) {
    frame_refcount[fidx] = r - 1;
    spin_unlock(&frame_lock);
    return;
  }
  frame_refcount[fidx] = 0;
  spin_unlock(&frame_lock);
  release_frame_to_pool(addr);
}
