/*
 * vfs_mount.c — Mount table for the fs_ops vtable. Maps absolute path
 * prefixes to (fs_ops, fs_priv) pairs and resolves arbitrary paths to
 * the owning mount via longest-prefix match.
 *
 * Invariants:
 *  - The mounts[] array is the sole authority for VFS dispatch; no
 *    adapter is reached except via a slot installed here.
 *  - target strings are validated at mount time: absolute, no trailing
 *    '/' except for the literal root "/". Adapter context is opaque to
 *    this TU.
 *  - Boot wiring runs single-threaded before secondary CPUs are up, so
 *    mounts[] does not require its own lock yet. A mount(2) syscall
 *    landing later must add one before the table can mutate at runtime.
 *  - Resolver matches with a '/' boundary so "/mnt" never claims
 *    "/mntfoo"; the "/" mount is the universal fallback.
 *
 * Not allowed:
 *  - Calling into ramfs_/ext2_ adapters from this TU; concrete
 *    fs_ops_t instances are built in later waves and installed via
 *    vfs_mount() at boot.
 *  - Allocating from the heap (table is static) or sleeping.
 *  - Exposing mounts[] outside kernel/fs/.
 */

#include <kernel/errno.h>
#include <kernel/fs_internal.h>
#include <kernel/kstring.h>
static mount_t mounts[VFS_MAX_MOUNTS];

static int mount_target_valid(const char *target, size_t *len_out) {
  if (!target || target[0] != '/')
    return 0;
  size_t len = kstrlen(target);
  if (len == 0 || len >= VFS_MOUNT_TARGET_MAX)
    return 0;
  if (len > 1 && target[len - 1] == '/')
    return 0;
  if (len_out)
    *len_out = len;
  return 1;
}

int vfs_mount(const char *target, const fs_ops_t *ops, void *fs_priv,
              uint32_t root_inode) {
  size_t tlen;
  if (!mount_target_valid(target, &tlen) || !ops || root_inode == 0)
    return -EINVAL;

  for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if (mounts[i].in_use && kstrcmp(mounts[i].target, target) == 0)
      return -EBUSY;
  }

  for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if (!mounts[i].in_use) {
      kstrncpy(mounts[i].target, target, VFS_MOUNT_TARGET_MAX);
      mounts[i].target[VFS_MOUNT_TARGET_MAX - 1] = '\0';
      mounts[i].ops = ops;
      mounts[i].fs_priv = fs_priv;
      mounts[i].root_inode = root_inode;
      mounts[i].in_use = true;
      return 0;
    }
  }
  return -ENOSPC;
}

static size_t mount_prefix_len(const char *target, size_t tlen,
                               const char *path) {
  if (tlen == 1 && target[0] == '/')
    return 1;

  for (size_t i = 0; i < tlen; i++) {
    if (path[i] != target[i])
      return 0;
  }
  char after = path[tlen];
  if (after != '\0' && after != '/')
    return 0;
  return tlen;
}

static int vfs_resolve_mount(const char *path, mount_t **out_mnt,
                             const char **out_rel) {
  if (!path || path[0] != '/' || !out_mnt || !out_rel)
    return -EINVAL;

  mount_t *best = NULL;
  size_t best_len = 0;

  for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if (!mounts[i].in_use)
      continue;
    size_t tlen = kstrlen(mounts[i].target);
    size_t mlen = mount_prefix_len(mounts[i].target, tlen, path);
    if (mlen > best_len) {
      best = &mounts[i];
      best_len = mlen;
    }
  }

  if (!best)
    return -ENOENT;

  *out_mnt = best;
  *out_rel = (best_len == 1) ? path + 1 : path + best_len;
  return 0;
}

static uint32_t resolve_one(mount_t *mnt, uint32_t cur_inode,
                            const char *comp) {
  if (comp[0] == '.' && comp[1] == '\0')
    return cur_inode;
  return mnt->ops->resolve(mnt->fs_priv, cur_inode, comp);
}

static uint32_t walk_components(mount_t *mnt, uint32_t cur_inode,
                                const char *rel) {
  if (!rel)
    return cur_inode;

  char comp[64];
  size_t ci = 0;
  const char *p = rel;

  while (*p) {
    if (*p == '/') {
      if (ci > 0) {
        comp[ci] = '\0';
        cur_inode = resolve_one(mnt, cur_inode, comp);
        if (cur_inode == 0)
          return 0;
        ci = 0;
      }
      p++;
    } else {
      if (ci < sizeof(comp) - 1)
        comp[ci++] = *p;
      p++;
    }
  }
  if (ci > 0) {
    comp[ci] = '\0';
    cur_inode = resolve_one(mnt, cur_inode, comp);
    if (cur_inode == 0)
      return 0;
  }
  return cur_inode;
}

int vfs_resolve_path(const char *abs_path, mount_t **out_mnt,
                     uint32_t *out_inode) {
  if (!abs_path || abs_path[0] != '/' || !out_mnt || !out_inode)
    return -EINVAL;

  mount_t *mnt = NULL;
  const char *rel = NULL;
  int rc = vfs_resolve_mount(abs_path, &mnt, &rel);
  if (rc < 0)
    return rc;
  if (!mnt->ops || !mnt->ops->resolve)
    return -ENOSYS;

  uint32_t ino = walk_components(mnt, mnt->root_inode, rel);
  if (ino == 0)
    return -ENOENT;

  *out_mnt = mnt;
  *out_inode = ino;
  return 0;
}

int vfs_resolve_parent_and_leaf(const char *abs_path, mount_t **out_mnt,
                                uint32_t *out_parent, const char **out_leaf) {
  if (!abs_path || abs_path[0] != '/' || !out_mnt || !out_parent || !out_leaf)
    return -EINVAL;

  size_t len = kstrlen(abs_path);
  if (len < 2 || abs_path[len - 1] == '/')
    return -EINVAL;

  size_t last_slash = 0;
  for (size_t i = len; i > 0; i--) {
    if (abs_path[i - 1] == '/') {
      last_slash = i - 1;
      break;
    }
  }

  const char *leaf = abs_path + last_slash + 1;
  if (*leaf == '\0')
    return -EINVAL;

  char parent_buf[512];
  if (last_slash == 0) {
    parent_buf[0] = '/';
    parent_buf[1] = '\0';
  } else {
    if (last_slash >= sizeof(parent_buf))
      return -ENAMETOOLONG;
    for (size_t i = 0; i < last_slash; i++)
      parent_buf[i] = abs_path[i];
    parent_buf[last_slash] = '\0';
  }

  mount_t *mnt = NULL;
  uint32_t parent_ino = 0;
  int rc = vfs_resolve_path(parent_buf, &mnt, &parent_ino);
  if (rc < 0)
    return rc;

  *out_mnt = mnt;
  *out_parent = parent_ino;
  *out_leaf = leaf;
  return 0;
}
