/*
 * page_dir.c — page-directory primitives: identity map, create/destroy/switch
 *              a per-process PD, install/remove PTEs, walk the tree.
 *
 * Invariants:
 *  - Every PTE installed for userspace carries PAGE_PRESENT | PAGE_USER;
 *    map_page promotes PAGE_USER into the parent PDE when the caller asks.
 *  - kernel_page_directory[] lives below IDENTITY_MAP_END so it is always
 *    addressable as a kernel pointer without temp mapping.
 *  - switch_page_directory writes CR3 only after the new PD is fully
 *    populated; map_uncached_mmio installs PTEs above the identity range
 *    only for ranges previously reserved in the mmio_pt pool.
 *
 * Not allowed:
 *  - Calling the VFS, the scheduler, or process_* helpers (PD is taken as
 *    a parameter, never fetched via get_current_process()).
 *  - Returning frames to the pool here; refcount + free_frame live in
 *    frame_alloc.c.
 */

#include <kernel/gdt.h>
#include <kernel/kprintf.h>
#include <kernel/memory.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>

#define PD_INDEX_MASK 0x3FF
#define PT_INDEX_MASK 0x3FF

uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));

static uint32_t kernel_page_table_0[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_1[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_2[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_3[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_4[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_5[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_6[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_7[1024] __attribute__((aligned(4096)));

uint32_t *kernel_page_tables[MAX_PAGE_TABLES] = {kernel_page_table_0,
                                                 kernel_page_table_1,
                                                 kernel_page_table_2,
                                                 kernel_page_table_3,
                                                 kernel_page_table_4,
                                                 kernel_page_table_5,
                                                 kernel_page_table_6,
                                                 kernel_page_table_7,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL};

void paging_init(void) {
  frame_alloc_reserve_low(KERNEL_HEAP_END);

  for (int i = 0; i < 1024; i++) {
    kernel_page_directory[i] = 0;
  }

  kprintf("Paging: Setting up %d page tables for %dMB identity mapping\n",
          NUM_KERNEL_PAGE_TABLES, (int)(IDENTITY_MAP_END / MB));

  for (uint32_t t = 0; t < NUM_KERNEL_PAGE_TABLES; t++) {
    if (!kernel_page_tables[t]) {
      kprintf("ERROR: Not enough static page tables! Need %d, have 3\n",
              NUM_KERNEL_PAGE_TABLES);
      return;
    }

    for (int p = 0; p < 1024; p++) {
      uint32_t addr = (t * 1024 + p) * PAGE_SIZE_CONST;
      kernel_page_tables[t][p] = addr | PAGE_PRESENT | PAGE_WRITABLE;
    }

    uint32_t pt_phys = (uint32_t)kernel_page_tables[t];
    kernel_page_directory[t] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;

    frame_alloc_mark_used(pt_phys);
  }

  kprintf("Paging: Identity mapped 0x%x - 0x%x\n", 0, IDENTITY_MAP_END);

  set_cr3((uint32_t)kernel_page_directory);
  uint32_t cr0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000;
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

#define MMIO_PT_POOL_SIZE 4
static uint32_t mmio_pts[MMIO_PT_POOL_SIZE][1024]
    __attribute__((aligned(4096)));
static uint32_t mmio_pt_next;

int map_uncached_mmio(uint32_t phys) {
  const uint32_t PTE_PCD = 0x010;
  const uint32_t pte_flags = PAGE_PRESENT | PAGE_WRITABLE | PTE_PCD;
  const uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITABLE;

  uint32_t pdi = phys >> 22;
  uint32_t pti = (phys >> 12) & 0x3FF;
  uint32_t page_phys = phys & PAGE_FRAME_MASK;

  uint32_t *pt;
  if (kernel_page_directory[pdi] & PAGE_PRESENT) {
    pt = (uint32_t *)(kernel_page_directory[pdi] & PAGE_FRAME_MASK);
  } else {
    if (mmio_pt_next >= MMIO_PT_POOL_SIZE)
      return -1;
    pt = mmio_pts[mmio_pt_next++];
    for (int i = 0; i < 1024; i++)
      pt[i] = 0;
    kernel_page_directory[pdi] = ((uint32_t)pt) | pde_flags;
  }

  uint32_t existing = pt[pti];
  if ((existing & PAGE_PRESENT) &&
      ((existing & PAGE_FRAME_MASK) == page_phys)) {
    return 0;
  }
  pt[pti] = page_phys | pte_flags;
  __asm__ volatile("invlpg (%0)" ::"r"(phys) : "memory");
  return 0;
}

page_directory_t *create_page_directory(void) {
  page_directory_t *dir = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  if (!dir)
    return NULL;

  uint32_t pd_phys = allocate_frame();
  if (pd_phys == (uint32_t)-1 || pd_phys >= IDENTITY_MAP_END) {
    if (pd_phys != (uint32_t)-1)
      free_frame(pd_phys);
    kfree(dir);
    return NULL;
  }

  dir->physical_addr = pd_phys;
  uint32_t *pd = (uint32_t *)pd_phys;

  for (int i = 0; i < 1024; i++) {
    dir->tables_physical[i] = 0;
    pd[i] = 0;
  }

  for (uint32_t i = 0; i < NUM_KERNEL_PAGE_TABLES; i++) {
    pd[i] = kernel_page_directory[i];
    dir->tables_physical[i] = kernel_page_directory[i];
  }

  for (uint32_t i = NUM_KERNEL_PAGE_TABLES; i < 1024; i++) {
    if (kernel_page_directory[i] & PAGE_PRESENT) {
      pd[i] = kernel_page_directory[i];
      dir->tables_physical[i] = kernel_page_directory[i];
    }
  }

  return dir;
}

void destroy_page_directory(page_directory_t *dir) {
  if (!dir)
    return;

  uint32_t *pd = (uint32_t *)dir->physical_addr;

  for (int i = 4; i < 768; i++) {
    if (!(pd[i] & PAGE_PRESENT))
      continue;

    uint32_t pt_phys = pd[i] & PAGE_FRAME_MASK;
    if (pt_phys >= IDENTITY_MAP_END)
      continue;

    uint32_t *pt = (uint32_t *)pt_phys;
    for (int j = 0; j < 1024; j++) {
      uint32_t entry = pt[j];
      if (entry & PAGE_PRESENT) {
        frame_decref(entry & PAGE_FRAME_MASK);
      }
    }
    free_frame(pt_phys);
  }

  free_frame(dir->physical_addr);
  kfree(dir);
}

void switch_page_directory(page_directory_t *dir) {
  set_cr3(dir->physical_addr);
}

int map_page(page_directory_t *dir, uint32_t virt, uint32_t phys,
             uint32_t flags) {
  uint32_t pdi = virt >> 22;
  uint32_t pti = (virt >> 12) & PT_INDEX_MASK;

  uint32_t *pd = (uint32_t *)dir->physical_addr;

  if (!(pd[pdi] & PAGE_PRESENT)) {
    uint32_t pt_phys = allocate_frame();
    if (pt_phys == (uint32_t)-1)
      return -1;
    zero_frame(pt_phys);

    pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    dir->tables_physical[pdi] = pd[pdi];
  }

  if (flags & PAGE_USER) {
    pd[pdi] |= PAGE_USER;
    dir->tables_physical[pdi] = pd[pdi];
  }

  uint32_t pt_phys = pd[pdi] & PAGE_FRAME_MASK;
  uint32_t *pt = (uint32_t *)pt_phys;
  pt[pti] = (phys & PAGE_FRAME_MASK) | flags;

  __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
  return 0;
}

void unmap_page(page_directory_t *dir, uint32_t virt) {
  uint32_t pdi = virt >> 22;
  uint32_t pti = (virt >> 12) & PT_INDEX_MASK;

  uint32_t *pd = (uint32_t *)dir->physical_addr;
  if (!(pd[pdi] & PAGE_PRESENT))
    return;

  uint32_t *pt = (uint32_t *)(pd[pdi] & PAGE_FRAME_MASK);
  pt[pti] = 0;
  __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t get_physical_addr(page_directory_t *dir, uint32_t virt) {
  uint32_t pdi = virt >> 22;
  uint32_t pti = (virt >> 12) & PT_INDEX_MASK;

  uint32_t *pd = (uint32_t *)dir->physical_addr;
  if (!(pd[pdi] & PAGE_PRESENT))
    return 0;

  uint32_t *pt = (uint32_t *)(pd[pdi] & PAGE_FRAME_MASK);
  if (!(pt[pti] & PAGE_PRESENT))
    return 0;

  return (pt[pti] & PAGE_FRAME_MASK) | (virt & PAGE_MASK);
}

uint32_t get_physical_addr_current(uint32_t virt) {
  uint32_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

  uint32_t pdi = virt >> 22;
  uint32_t pti = (virt >> 12) & PT_INDEX_MASK;

  uint32_t *pd = (uint32_t *)(cr3 & PAGE_FRAME_MASK);
  if (!(pd[pdi] & PAGE_PRESENT))
    return 0;

  uint32_t *pt = (uint32_t *)(pd[pdi] & PAGE_FRAME_MASK);
  if (!(pt[pti] & PAGE_PRESENT))
    return 0;

  return (pt[pti] & PAGE_FRAME_MASK) | (virt & PAGE_MASK);
}
