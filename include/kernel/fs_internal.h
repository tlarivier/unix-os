#ifndef KERNEL_FS_INTERNAL_H
#define KERNEL_FS_INTERNAL_H

#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

struct stat;
struct linux_dirent;

typedef struct fs_ops {
  uint32_t (*resolve)(void *fs_priv, uint32_t parent, const char *name);
  ssize_t (*read)(void *fs_priv, uint32_t inode, void *buf, size_t count,
                  uint32_t offset);
  ssize_t (*write)(void *fs_priv, uint32_t inode, const void *buf, size_t count,
                   uint32_t offset);
  int (*stat)(void *fs_priv, uint32_t inode, struct stat *st);
  ssize_t (*readdir)(void *fs_priv, uint32_t inode, void *buf, size_t size,
                     uint32_t *pos_out);
  int (*mkdir)(void *fs_priv, uint32_t parent, const char *name, uint32_t mode,
               uint32_t *out_inode);
  int (*create)(void *fs_priv, uint32_t parent, const char *name, uint32_t mode,
                uint32_t *out_inode);
  int (*unlink)(void *fs_priv, uint32_t parent, const char *name);
  int (*rmdir)(void *fs_priv, uint32_t parent, const char *name);
  int (*truncate)(void *fs_priv, uint32_t inode, uint32_t length);
  int (*chmod)(void *fs_priv, uint32_t inode, uint32_t mode);
  int (*chown)(void *fs_priv, uint32_t inode, uint32_t uid, uint32_t gid);
  int (*rename)(void *fs_priv, uint32_t old_parent, const char *old_name,
                uint32_t new_parent, const char *new_name);
  uint8_t (*dtype)(void *fs_priv, uint32_t inode);
  void (*inc_open)(void *fs_priv, uint32_t inode);
  void (*dec_open)(void *fs_priv, uint32_t inode);
  uint32_t (*get_open)(void *fs_priv, uint32_t inode);
  void (*release)(void *fs_priv, uint32_t inode);
} fs_ops_t;

#define VFS_MAX_MOUNTS 8
#define VFS_MOUNT_TARGET_MAX 64

typedef struct mount {
  char target[VFS_MOUNT_TARGET_MAX];
  const fs_ops_t *ops;
  void *fs_priv;
  uint32_t root_inode;
  bool in_use;
} mount_t;

int vfs_mount(const char *target, const fs_ops_t *ops, void *fs_priv,
              uint32_t root_inode);

int vfs_resolve_path(const char *abs_path, mount_t **out_mnt,
                     uint32_t *out_inode);

int vfs_resolve_parent_and_leaf(const char *abs_path, mount_t **out_mnt,
                                uint32_t *out_parent, const char **out_leaf);

#endif /* KERNEL_FS_INTERNAL_H */
