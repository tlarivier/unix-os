#ifndef KERNEL_VFS_INTERNAL_H
#define KERNEL_VFS_INTERNAL_H

#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <stdint.h>

#define VFS_MAX_OPEN_FILES 64

struct fs_ops;
typedef struct vfs_open_file {
  uint32_t inode;
  uint32_t pos;
  uint32_t flags;
  uint32_t refcount;
  const struct fs_ops *fs;
  void *fs_priv;
} vfs_open_file_t;

extern vfs_open_file_t open_files[VFS_MAX_OPEN_FILES];
extern spinlock_t vfs_lock;

int vfs_alloc_open_file(void);
void vfs_free_open_file(int idx);

#endif /* KERNEL_VFS_INTERNAL_H */
