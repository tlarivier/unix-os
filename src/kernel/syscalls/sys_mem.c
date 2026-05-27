/*
 * sys_mem.c — marshalling for sys_brk / sys_mmap / sys_munmap / sys_mprotect,
 * delegating heap growth, anonymous and file-backed mappings, framebuffer
 * identity mapping, unmap and protection changes with TLB shootdown.
 *
 * Invariants:
 *  - Uniform (uint32_t x5) -> int32_t ABI on every wrapper.
 *  - PROT_* / MAP_* values come from <uapi/mman.h> (single source of truth).
 *  - All page-table edits go through map_page/unmap_page + smp_tlb_flush_all.
 *  - Return value is the new break / mapped address, or a negative errno.
 *
 * Not allowed:
 *  - Talking to userspace memory without copy_*_user helpers.
 *  - Bypassing the mm/paging API (no direct PTE writes, no raw invlpg here).
 *  - Holding a spinlock across allocate_frame() or map_page() loops.
 */

#include "syscall.h"

#include <kernel/constants.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/paging.h>
#include <kernel/process.h>
#include <kernel/smp.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <uapi/mman.h>

#define PAGE_ADDR_MASK (~(PAGE_SIZE_CONST - 1))
#define MAX_HEAP_SIZE HEAP_MAX_SIZE
#define MMAP_BASE USER_HEAP_BASE

static void unmap_range(process_memory_t *mem, uint32_t a, uint32_t b) {
  for (uint32_t p = a; p < b; p += PAGE_SIZE_CONST) {
    uint32_t phys = get_physical_addr(mem->page_directory, p);
    if (phys) {
      unmap_page(mem->page_directory, p);
      free_frame(phys);
    }
  }
}

int32_t sys_brk(uint32_t addr, uint32_t u2, uint32_t u3, uint32_t u4,
                uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  process_t *cur = get_current_process();
  if (!cur->memory)
    return -ENOMEM;

  if (addr == 0)
    return (int32_t)cur->memory->brk;
  if (addr < HEAP_START)
    return -EINVAL;
  if (addr > HEAP_START + MAX_HEAP_SIZE)
    return -ENOMEM;

  uint32_t old_brk = cur->memory->brk;
  uint32_t new_brk = addr;

  if (new_brk > old_brk) {
    uint32_t start_page = (old_brk + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;
    uint32_t end_page = (new_brk + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;
    uint32_t page = start_page;
    int failed = 0;

    for (; page < end_page; page += PAGE_SIZE_CONST) {
      uint32_t frame = allocate_frame();
      if (frame == (uint32_t)-1) {
        failed = 1;
        break;
      }

      uint32_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
      if (map_page(cur->memory->page_directory, page, frame, flags) < 0) {
        free_frame(frame);
        failed = 1;
        break;
      }
    }

    if (failed) {
      unmap_range(cur->memory, start_page, page);
      return -ENOMEM;
    }
  } else if (new_brk < old_brk) {
    uint32_t start_page = (new_brk + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;
    uint32_t end_page = (old_brk + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;
    unmap_range(cur->memory, start_page, end_page);
  }

  cur->memory->brk = new_brk;
  return (int32_t)new_brk;
}

static int is_fb_device(int fd) {
  const char *p = (fd < 0) ? NULL : vfs_get_path_by_fd(fd);
  return p && kstrncmp(p, "/dev/fb", 7) == 0 &&
         (p[7] == '\0' || (p[7] == '0' && p[8] == '\0'));
}

int32_t sys_mmap(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags,
                 uint32_t fd) {
  if (len == 0)
    return -EINVAL;
  if (len > 256u * 1024u * 1024u)
    return -ENOMEM;

  process_t *cur = get_current_process();
  if (!cur->memory)
    return -ENOMEM;

  len = (len + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;

  if (((flags & MAP_FIXED) && addr == VGA_FRAMEBUFFER_ADDR) ||
      ((int)fd >= 0 && is_fb_device((int)fd))) {
    uint32_t fb_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    for (uint32_t off = 0; off < len && off < VGA_FRAMEBUFFER_SIZE;
         off += PAGE_SIZE_CONST) {
      map_page(cur->memory->page_directory, VGA_FRAMEBUFFER_ADDR + off,
               VGA_FRAMEBUFFER_ADDR + off, fb_flags);
    }
    return (int32_t)VGA_FRAMEBUFFER_ADDR;
  }

  int slot = -1;
  for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
    if (!cur->memory->mmap_regions[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return -ENOMEM;

  uint32_t map_addr;
  int use_fixed = (flags & MAP_FIXED) && addr != 0;
  if (use_fixed) {
    map_addr = addr & PAGE_ADDR_MASK;
    if (map_addr < USER_CODE_BASE || map_addr >= USER_SPACE_END ||
        len > USER_SPACE_END - map_addr)
      return -EINVAL;
  } else {
    if (cur->memory->mmap_next_addr == 0)
      cur->memory->mmap_next_addr = MMAP_BASE;
    map_addr = cur->memory->mmap_next_addr;
    if (map_addr < USER_CODE_BASE || map_addr >= USER_SPACE_END ||
        len > USER_SPACE_END - map_addr)
      return -ENOMEM;
    cur->memory->mmap_next_addr += len;
  }

  mmap_region_t *r = &cur->memory->mmap_regions[slot];

  if (!(flags & MAP_ANON) && (int)fd >= 0) {
    if (mmap_file_register(map_addr, map_addr + len, (int)fd, 0, prot) < 0) {
      return -ENOMEM;
    }
    r->addr = map_addr;
    r->len = len;
    r->prot = prot;
    r->flags = flags;
    r->in_use = 1;
    return (int32_t)map_addr;
  }

  uint32_t pflags =
      PAGE_PRESENT | PAGE_USER | ((prot & PROT_WRITE) ? PAGE_WRITABLE : 0);
  for (uint32_t off = 0; off < len; off += PAGE_SIZE_CONST) {
    uint32_t f = allocate_frame();
    if (f == (uint32_t)-1 ||
        map_page(cur->memory->page_directory, map_addr + off, f, pflags) < 0) {
      if (f != (uint32_t)-1)
        free_frame(f);
      unmap_range(cur->memory, map_addr, map_addr + off);
      return -ENOMEM;
    }
    zero_frame(f); /* infoleak protection */
  }

  r->addr = map_addr;
  r->len = len;
  r->prot = prot;
  r->flags = flags;
  r->in_use = 1;
  return (int32_t)map_addr;
}

int32_t sys_munmap(uint32_t addr, uint32_t len, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (addr == 0 || len == 0)
    return -EINVAL;

  process_t *cur = get_current_process();
  if (!cur->memory)
    return -ESRCH;

  len = (len + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;

  mmap_region_t *regions = cur->memory->mmap_regions;
  for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
    if (regions[i].in_use && regions[i].addr == addr) {
      uint32_t flags = local_irq_save();
      mmap_file_unregister(regions[i].addr, regions[i].addr + regions[i].len);
      unmap_range(cur->memory, regions[i].addr,
                  regions[i].addr + regions[i].len);
      regions[i].in_use = 0;
      local_irq_restore(flags);
      return 0;
    }
  }
  return -EINVAL;
}

int32_t sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot, uint32_t u4,
                     uint32_t u5) {
  (void)u4;
  (void)u5;

  if (addr == 0 || len == 0)
    return -EINVAL;
  if (addr & ~PAGE_ADDR_MASK)
    return -EINVAL;
  if (prot & ~(uint32_t)(PROT_READ | PROT_WRITE | PROT_EXEC))
    return -EINVAL;

  process_t *cur = get_current_process();
  if (!cur->memory)
    return -ESRCH;

  len = (len + PAGE_SIZE_CONST - 1) & PAGE_ADDR_MASK;

  if (addr < USER_CODE_BASE)
    return -EINVAL;
  if (len > USER_SPACE_END - addr)
    return -EINVAL;

  for (uint32_t offset = 0; offset < len; offset += PAGE_SIZE_CONST) {
    uint32_t phys =
        get_physical_addr(cur->memory->page_directory, addr + offset);
    if (phys) {
      uint32_t flags = PAGE_USER;
      if (prot != PROT_NONE)
        flags |= PAGE_PRESENT;
      if (prot & PROT_WRITE)
        flags |= PAGE_WRITABLE;
      map_page(cur->memory->page_directory, addr + offset, phys, flags);
      __asm__ volatile("invlpg (%0)" ::"r"(addr + offset) : "memory");
    }
  }

  smp_tlb_flush_all();

  return 0;
}
