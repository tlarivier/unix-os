/*
 * cow_temp.c — copy-on-write fault handler and single-slot temp mapping for
 *              frames above the identity-mapped range.
 *
 * Invariants:
 *  - temp_map_frame holds temp_map_lock until the matching temp_unmap_frame;
 *    at most one temporary mapping is live at any moment.
 *  - After handle_cow_fault returns, the faulting PTE is PAGE_PRESENT |
 *    PAGE_WRITABLE with PAGE_COW cleared; the writable bit is restored
 *    only once the frame is sole-owned (refcount == 1) or has been copied.
 *  - Lock order: cow_lock > frame_lock; temp_map_lock is independent and
 *    is never taken while holding either of the above.
 *
 * Not allowed:
 *  - Calling the VFS or the scheduler (PD is passed in by the caller).
 *  - Exposing temp_map_frame to consumers outside kernel/mm/ (the
 *    "lock-held-on-success" contract is fragile).
 */

#include <kernel/kernel.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/spinlock.h>

#define PD_INDEX_MASK 0x3FF
#define PT_INDEX_MASK 0x3FF

#define TEMP_MAP_VADDR (IDENTITY_MAP_END - PAGE_SIZE_CONST)

static spinlock_t temp_map_lock = SPINLOCK_INIT("temp_map");
static spinlock_t cow_lock = SPINLOCK_INIT("cow");

void *temp_map_frame(uint32_t phys_frame) {
  spin_lock(&temp_map_lock);

  if (phys_frame < IDENTITY_MAP_END) {
    return (void *)phys_frame;
  }

  uint32_t pdi = (TEMP_MAP_VADDR >> 22) & PD_INDEX_MASK;
  uint32_t pti = (TEMP_MAP_VADDR >> 12) & PT_INDEX_MASK;

  if (pdi < NUM_KERNEL_PAGE_TABLES && kernel_page_tables[pdi]) {
    kernel_page_tables[pdi][pti] =
        (phys_frame & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;

    __asm__ volatile("invlpg (%0)" : : "r"(TEMP_MAP_VADDR) : "memory");

    return (void *)TEMP_MAP_VADDR;
  }

  spin_unlock(&temp_map_lock);
  return NULL;
}

void temp_unmap_frame(void) {
  uint32_t pdi = (TEMP_MAP_VADDR >> 22) & PD_INDEX_MASK;
  uint32_t pti = (TEMP_MAP_VADDR >> 12) & PT_INDEX_MASK;

  if (pdi < NUM_KERNEL_PAGE_TABLES && kernel_page_tables[pdi]) {
    kernel_page_tables[pdi][pti] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(TEMP_MAP_VADDR) : "memory");
  }

  spin_unlock(&temp_map_lock);
}

void zero_frame(uint32_t addr) {
  if (addr < IDENTITY_MAP_END) {
    uint32_t *p = (uint32_t *)addr;
    for (int i = 0; i < 1024; i++)
      p[i] = 0;
  } else {
    uint32_t *p = (uint32_t *)temp_map_frame(addr);
    if (!p) {
      KERNEL_PANIC("zero_frame: temp_map_frame slot exhausted");
    }
    for (int i = 0; i < 1024; i++)
      p[i] = 0;
    temp_unmap_frame();
  }
}

void *copy_to_frame(uint32_t frame, uint32_t offset, const void *src,
                    size_t len) {
  if (frame < IDENTITY_MAP_END) {
    uint8_t *dst = (uint8_t *)(frame + offset);
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
      dst[i] = s[i];
    }
    return dst;
  } else {
    uint8_t *mapped = (uint8_t *)temp_map_frame(frame);
    if (!mapped) {
      KERNEL_PANIC("copy_to_frame: temp_map_frame slot exhausted");
    }
    uint8_t *dst = mapped + offset;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < len; i++) {
      dst[i] = s[i];
    }
    temp_unmap_frame();
    return (void *)1;
  }
}

int handle_cow_fault(page_directory_t *dir, uint32_t fault_addr) {
  if (!dir)
    return -1;

  uint32_t *pd = (uint32_t *)dir->physical_addr;
  uint32_t pdi = (fault_addr >> 22) & PD_INDEX_MASK;
  uint32_t pti = (fault_addr >> 12) & PT_INDEX_MASK;

  if (!(pd[pdi] & PAGE_PRESENT))
    return -1;
  uint32_t pt_phys = pd[pdi] & PAGE_FRAME_MASK;
  if (pt_phys >= IDENTITY_MAP_END)
    return -1;
  uint32_t *pt = (uint32_t *)pt_phys;

  uint32_t entry = pt[pti];
  if (!(entry & PAGE_PRESENT))
    return -1;
  if (!(entry & PAGE_COW_BIT))
    return -1; /* not a CoW page -> real SEGV
               (or already resolved) */

  spin_lock(&cow_lock);

  entry = pt[pti];
  if (!(entry & PAGE_PRESENT) || !(entry & PAGE_COW_BIT)) {
    spin_unlock(&cow_lock);
    return 0;
  }

  uint32_t old_frame = entry & PAGE_FRAME_MASK;
  uint32_t flags = entry & PAGE_MASK;

  if (frame_getref(old_frame) <= 1) {
    flags = (flags & ~(uint32_t)PAGE_COW_BIT) | PAGE_WRITABLE;
    pt[pti] = old_frame | flags;
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    spin_unlock(&cow_lock);
    return 0;
  }

  uint32_t new_frame = allocate_frame();
  if (new_frame == (uint32_t)-1) {
    spin_unlock(&cow_lock);
    return -1;
  }

  const uint32_t *s;
  int s_temp = 0;
  if (old_frame < IDENTITY_MAP_END) {
    s = (const uint32_t *)old_frame;
  } else {
    s = (const uint32_t *)temp_map_frame(old_frame);
    if (!s) {
      free_frame(new_frame);
      spin_unlock(&cow_lock);
      return -1;
    }
    s_temp = 1;
  }

  if (new_frame < IDENTITY_MAP_END) {
    uint32_t *d = (uint32_t *)new_frame;
    for (int k = 0; k < 1024; k++)
      d[k] = s[k];
  } else {
    uint32_t buf[1024];
    for (int k = 0; k < 1024; k++)
      buf[k] = s[k];
    if (s_temp) {
      temp_unmap_frame();
      s_temp = 0;
    }
    uint32_t *d = (uint32_t *)temp_map_frame(new_frame);
    if (!d) {
      free_frame(new_frame);
      spin_unlock(&cow_lock);
      return -1;
    }
    for (int k = 0; k < 1024; k++)
      d[k] = buf[k];
    temp_unmap_frame();
  }
  if (s_temp)
    temp_unmap_frame();

  flags = (flags & ~(uint32_t)PAGE_COW_BIT) | PAGE_WRITABLE;
  pt[pti] = new_frame | flags;
  {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
  }

  frame_decref(old_frame);
  spin_unlock(&cow_lock);
  return 0;
}
