/*
 * mmap_file.c — file-backed mmap registry and demand-paging fault handler.
 *
 * Invariants:
 *  - Every region key {pd_phys, [start,end)} stored in mmap_file_table is
 *    page-aligned; start and end satisfy (x & PAGE_MASK) == 0 (V30).
 *  - mmap_file_table and the lookup walk are guarded by mmap_file_lock;
 *    the lock is held only across O(MMAP_FILE_MAX_REGIONS) scans, never
 *    across vfs_pread or temp_map_frame.
 *  - On fault, a fresh frame is allocate_frame()d, zero_frame()d, filled
 *    via vfs_pread under temp_map_frame, then mapped with PAGE_PRESENT |
 *    PAGE_USER (write bit only if MMAP_FILE_PROT_WRITE was registered).
 *
 * Not allowed:
 *  - Touching scheduler/process internals beyond reading the current PD
 *    phys via get_current_process()->memory->page_directory.
 *  - Calling kmalloc on the fault path (registry is a fixed-size array).
 */

#include <kernel/constants.h>
#include <kernel/memory.h>
#include <kernel/memory_layout.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <stddef.h>
#include <stdint.h>

#define MMAP_FILE_MAX_REGIONS 128
#define MMAP_FILE_PROT_WRITE 0x2

typedef struct mmap_file_region {
  int in_use;
  uint32_t pd_phys;
  uint32_t start;
  uint32_t end;
  int fd;
  uint32_t offset;
  uint32_t prot;
} mmap_file_region_t;

static mmap_file_region_t mmap_file_table[MMAP_FILE_MAX_REGIONS];
static spinlock_t mmap_file_lock = SPINLOCK_INIT("mmap_file");

static uint32_t current_pd_phys(void) {
  process_t *cur = get_current_process();
  if (!cur || !cur->memory || !cur->memory->page_directory)
    return 0;
  return cur->memory->page_directory->physical_addr;
}

int mmap_file_register(uint32_t start, uint32_t end, int fd, uint32_t offset,
                       uint32_t prot) {
  if (end <= start || fd < 0)
    return -1;
  if ((start & PAGE_MASK) || (end & PAGE_MASK))
    return -1;

  uint32_t pd_phys = current_pd_phys();
  if (!pd_phys)
    return -1;

  spin_lock(&mmap_file_lock);
  for (int i = 0; i < MMAP_FILE_MAX_REGIONS; i++) {
    if (!mmap_file_table[i].in_use) {
      mmap_file_table[i].in_use = 1;
      mmap_file_table[i].pd_phys = pd_phys;
      mmap_file_table[i].start = start;
      mmap_file_table[i].end = end;
      mmap_file_table[i].fd = fd;
      mmap_file_table[i].offset = offset;
      mmap_file_table[i].prot = prot;
      spin_unlock(&mmap_file_lock);
      return 0;
    }
  }
  spin_unlock(&mmap_file_lock);
  return -1;
}

void mmap_file_unregister(uint32_t start, uint32_t end) {
  uint32_t pd_phys = current_pd_phys();
  if (!pd_phys)
    return;

  spin_lock(&mmap_file_lock);
  for (int i = 0; i < MMAP_FILE_MAX_REGIONS; i++) {
    if (mmap_file_table[i].in_use && mmap_file_table[i].pd_phys == pd_phys &&
        mmap_file_table[i].start == start && mmap_file_table[i].end == end) {
      mmap_file_table[i].in_use = 0;
      break;
    }
  }
  spin_unlock(&mmap_file_lock);
}

void mmap_file_clear_pd(uint32_t pd_phys) {
  spin_lock(&mmap_file_lock);
  for (int i = 0; i < MMAP_FILE_MAX_REGIONS; i++) {
    if (mmap_file_table[i].in_use && mmap_file_table[i].pd_phys == pd_phys) {
      mmap_file_table[i].in_use = 0;
    }
  }
  spin_unlock(&mmap_file_lock);
}

static mmap_file_region_t *mmap_file_lookup(uint32_t pd_phys, uint32_t addr) {
  for (int i = 0; i < MMAP_FILE_MAX_REGIONS; i++) {
    if (mmap_file_table[i].in_use && mmap_file_table[i].pd_phys == pd_phys &&
        addr >= mmap_file_table[i].start && addr < mmap_file_table[i].end) {
      return &mmap_file_table[i];
    }
  }
  return NULL;
}

int handle_demand_fault(uint32_t fault_addr) {
  process_t *cur = get_current_process();
  if (!cur || !cur->memory || !cur->memory->page_directory)
    return -1;
  page_directory_t *pd = cur->memory->page_directory;
  uint32_t pd_phys = pd->physical_addr;

  spin_lock(&mmap_file_lock);
  mmap_file_region_t *r = mmap_file_lookup(pd_phys, fault_addr);
  if (!r) {
    spin_unlock(&mmap_file_lock);
    return -1;
  }
  int fd = r->fd;
  uint32_t r_start = r->start;
  uint32_t r_off = r->offset;
  uint32_t r_prot = r->prot;
  spin_unlock(&mmap_file_lock);

  uint32_t page_va = fault_addr & PAGE_FRAME_MASK;
  uint32_t file_off = r_off + (page_va - r_start);

  if (get_physical_addr(pd, page_va) != 0) {
    return 0;
  }

  uint32_t frame = allocate_frame();
  if (frame == (uint32_t)-1)
    return -1;
  zero_frame(frame);

  int high = (frame >= IDENTITY_MAP_END);
  uint8_t *dst;
  if (high) {
    dst = (uint8_t *)temp_map_frame(frame);
    if (!dst) {
      free_frame(frame);
      return -1;
    }
  } else {
    dst = (uint8_t *)frame;
  }

  ssize_t n = vfs_pread(fd, dst, PAGE_SIZE_CONST, file_off);
  (void)n; /* short reads / EOF are fine - tail stays zero-filled */

  if (high)
    temp_unmap_frame();

  uint32_t flags = PAGE_PRESENT | PAGE_USER;
  if (r_prot & MMAP_FILE_PROT_WRITE)
    flags |= PAGE_WRITABLE;

  if (get_physical_addr(pd, page_va) != 0) {
    free_frame(frame);
    return 0;
  }

  if (map_page(pd, page_va, frame, flags) < 0) {
    free_frame(frame);
    return -1;
  }
  return 0;
}
