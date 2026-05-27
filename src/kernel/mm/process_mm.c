/*
 * process_mm.c — process_memory_t lifecycle: create, fork-clone (CoW),
 *                destroy, and pre-map user regions.
 *
 * Invariants:
 *  - After clone_process_memory, every user PTE that was originally
 *    writable is read-only + PAGE_COW in BOTH parent and child; PAGE_WRITABLE
 *    is restored only by handle_cow_fault once the frame is split.
 *  - Every frame retained across fork is frame_incref()ed in the child
 *    before the parent PTE is downgraded; the order parent->incref->child
 *    is preserved to keep refcount >= 1 throughout.
 *  - destroy_process_memory frees frames only via free_frame / frame_decref
 *    (never raw bitmap writes) and clears the matching mmap_file regions
 *    before tearing down the PD.
 *
 * Not allowed:
 *  - Calling the VFS or the scheduler (process_t is touched only via the
 *    opaque process_memory_t pointer; no schedule(), no proc_table_lock).
 *  - Mapping user pages above KERNEL_BASE or below USER_MIN_VA.
 */

#include <kernel/constants.h>
#include <kernel/memory.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/process_mm.h>
#include <stddef.h>
#include <stdint.h>

#define USER_PDE_FIRST NUM_KERNEL_PAGE_TABLES

static int map_user_pages(process_memory_t *mem, uint32_t virtual_base,
                          uint32_t size, uint32_t flags);

process_memory_t *create_process_memory(void) {
  process_memory_t *mem = (process_memory_t *)kmalloc(sizeof(process_memory_t));
  if (!mem)
    return NULL;

  mem->page_directory = create_page_directory();
  if (!mem->page_directory) {
    kfree(mem);
    return NULL;
  }

  mem->brk = USER_CODE_BASE;
  mem->stack_base = USER_STACK_BASE;
  mem->mmap_next_addr = USER_HEAP_BASE;

  for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
    mem->mmap_regions[i].in_use = 0;
  }

  uint32_t stack_size = 32 * PAGE_SIZE_CONST; /* 128 KiB */
  uint32_t stack_bottom = USER_STACK_BASE - stack_size;
  if (map_user_pages(mem, stack_bottom, stack_size,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
    destroy_page_directory(mem->page_directory);
    kfree(mem);
    return NULL;
  }

  return mem;
}

static void purge_dst_user_pages(process_memory_t *dst) {
  uint32_t *dst_pd = (uint32_t *)dst->page_directory->physical_addr;
  for (int i = USER_PDE_FIRST; i < USER_PDE_LAST; i++) {
    if (!(dst_pd[i] & PAGE_PRESENT))
      continue;
    uint32_t pt_phys = dst_pd[i] & PAGE_FRAME_MASK;
    if (pt_phys >= IDENTITY_MAP_END)
      continue;
    uint32_t *pt = (uint32_t *)pt_phys;
    for (int j = 0; j < 1024; j++) {
      if (pt[j] & PAGE_PRESENT) {
        frame_decref(pt[j] & PAGE_FRAME_MASK);
        pt[j] = 0;
      }
    }
    free_frame(pt_phys);
    dst_pd[i] = 0;
    dst->page_directory->tables_physical[i] = 0;
  }
}

static int cow_share_user_pages(process_memory_t *src, process_memory_t *dst) {
  uint32_t *src_pd = (uint32_t *)src->page_directory->physical_addr;

  for (int i = USER_PDE_FIRST; i < USER_PDE_LAST; i++) {
    if (!(src_pd[i] & PAGE_PRESENT))
      continue;

    uint32_t src_pt_phys = src_pd[i] & PAGE_FRAME_MASK;
    if (src_pt_phys >= IDENTITY_MAP_END)
      continue;

    uint32_t *src_pt = (uint32_t *)src_pt_phys;

    for (int j = 0; j < 1024; j++) {
      if (!(src_pt[j] & PAGE_PRESENT))
        continue;

      uint32_t entry = src_pt[j];
      uint32_t frame = entry & PAGE_FRAME_MASK;
      uint32_t flags = entry & PAGE_MASK;
      uint32_t virt = ((uint32_t)i << 22) | ((uint32_t)j << 12);

      if (flags & PAGE_WRITABLE) {
        flags = (flags & ~(uint32_t)PAGE_WRITABLE) | PAGE_COW_BIT;
        src_pt[j] = frame | flags;
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
      }

      if (map_page(dst->page_directory, virt, frame, flags) < 0) {
        return -1;
      }
      frame_incref(frame);
    }
  }
  return 0;
}

process_memory_t *clone_process_memory(process_memory_t *src) {
  if (!src || !src->page_directory)
    return NULL;
  process_memory_t *dst = create_process_memory();
  if (!dst)
    return NULL;

  dst->brk = src->brk;
  dst->stack_base = src->stack_base;
  dst->mmap_next_addr = src->mmap_next_addr;

  for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
    dst->mmap_regions[i] = src->mmap_regions[i];
  }

  purge_dst_user_pages(dst);

  if (cow_share_user_pages(src, dst) < 0) {
    destroy_process_memory(dst);
    return NULL;
  }

  uint32_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");

  return dst;
}

void destroy_process_memory(process_memory_t *mem) {
  if (!mem)
    return;
  if (mem->page_directory) {
    mmap_file_clear_pd(mem->page_directory->physical_addr);
    destroy_page_directory(mem->page_directory);
  }
  kfree(mem);
}

static int map_user_pages(process_memory_t *mem, uint32_t base, uint32_t size,
                          uint32_t flags) {
  if (!mem)
    return -1;

  uint32_t start = base & PAGE_FRAME_MASK;
  uint32_t end = (base + size + PAGE_SIZE_CONST - 1) & PAGE_FRAME_MASK;

  for (uint32_t addr = start; addr < end; addr += PAGE_SIZE_CONST) {
    uint32_t frame = allocate_frame();
    if (frame == (uint32_t)-1)
      return -1;
    zero_frame(frame);

    if (map_page(mem->page_directory, addr, frame, flags) < 0) {
      free_frame(frame);
      return -1;
    }
  }
  return 0;
}
