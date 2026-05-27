/*
 * vfs_core.c — Owns the global VFS state (open_files[] table and vfs_lock),
 * bootstraps ramfs+dcache via vfs_init, and provides the open-file slot
 * allocator/refcount used by vfs_fd, vfs_path and fork().
 *
 * Invariants:
 *  - open_files[] and vfs_lock are the canonical shared state; mutations
 *    require vfs_lock held by the caller.
 *  - of_idx is stored encoded (raw_idx + 1) in proc_fd_t.of_idx; raw form
 *    is used only inside fs/.
 *  - vfs_init runs exactly once at boot before any other vfs_* entry.
 *
 * Not allowed:
 *  - Calling schedule() (directly or via synchronize_rcu) while holding
 *    vfs_lock.
 *  - Exposing open_files[] or vfs_open_file_t outside kernel/fs/.
 *  - Allocating from IRQ context.
 */
#include <kernel/dcache.h>
#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/fs_internal.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/ramfs.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <kernel/vfs_internal.h>

vfs_open_file_t open_files[VFS_MAX_OPEN_FILES];
spinlock_t vfs_lock = SPINLOCK_INIT("vfs");

int vfs_alloc_open_file(void) {
  for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
    if (open_files[i].refcount == 0) {
      kmemset(&open_files[i], 0, sizeof(vfs_open_file_t));
      open_files[i].refcount = 1;
      return i;
    }
  }
  return -EMFILE;
}

void vfs_free_open_file(int idx) { open_files[idx].refcount = 0; }

int32_t vfs_init(void) {
  ramfs_init();
  dcache_init();

  int mrc = vfs_mount("/", &ramfs_ops, NULL, ramfs_root_inode());
  if (mrc < 0) {
    kprintf("VFS: ramfs mount at / failed: %d\n", mrc);
    return mrc;
  }

  kprintf("VFS initialized (ramfs at /, %d inodes, dcache)\n",
          RAMFS_MAX_INODES);
  return 0;
}

void vfs_open_file_incref(int raw_of_idx) {
  if (raw_of_idx < 0 || (uint32_t)raw_of_idx >= VFS_MAX_OPEN_FILES)
    return;

  spin_lock(&vfs_lock);
  if (open_files[raw_of_idx].refcount > 0) {
    open_files[raw_of_idx].refcount++;
  }
  spin_unlock(&vfs_lock);
}

uint8_t vfs_inode_dtype(uint32_t inode_idx) {
  return ramfs_inode_dtype(inode_idx);
}

int vfs_mount_ext2_at(const char *target) {
  if (!target)
    return -EINVAL;
  if (!ext2_is_mounted())
    return -ENODEV;
  return vfs_mount(target, &ext2_ops, NULL, 2);
}
