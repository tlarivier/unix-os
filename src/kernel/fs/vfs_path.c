/*
 * vfs_path.c — Path-keyed VFS operations (mkdir, rmdir, unlink, rename,
 * truncate, chmod, chown, chdir).
 *
 * Invariants:
 *  - vfs_lock is held across resolve+mutate sequences (chmod/chown/
 *    truncate/unlink/rename/rmdir) to prevent a concurrent free between
 *    path-to-inode and the inode write.
 *  - Every fs touch goes through mnt->ops->X(mnt->fs_priv, ...). Path
 *    walking funnels through vfs_resolve_path / vfs_resolve_parent_and_leaf
 *    so adapters stay behind the vtable.
 *  - dcache_invalidate runs OUTSIDE vfs_lock because synchronize_rcu may
 *    schedule; callers snapshot the path then drop the lock first.
 *  - inode 0 is the cross-fs "not found" sentinel.
 *
 * Not allowed:
 *  - Holding vfs_lock across a sleeping primitive (synchronize_rcu,
 *    schedule, wait_*).
 *  - Reaching into the scheduler/process state beyond reading
 *    current_process->cwd.
 *  - Calling ext2 or journal write paths directly.
 */

#include <../uapi/syscalls.h>
#include <kernel/dcache.h>
#include <kernel/errno.h>
#include <kernel/fs_internal.h>
#include <kernel/kstring.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/vfs_internal.h>

static int make_abs_path(const char *path, char *buf, size_t bufsz) {
  if (!path || bufsz < 2)
    return -EINVAL;
  if (path[0] == '/') {
    size_t i = 0;
    while (path[i] && i < bufsz - 1) {
      buf[i] = path[i];
      i++;
    }
    if (path[i] != '\0')
      return -ENAMETOOLONG;
    buf[i] = '\0';
    return 0;
  }
  process_t *cur = get_current_process();
  if (!cur || cur->cwd[0] == '\0') {
    buf[0] = '/';
    size_t i = 0;
    while (path[i] && i < bufsz - 2) {
      buf[1 + i] = path[i];
      i++;
    }
    if (path[i] != '\0')
      return -ENAMETOOLONG;
    buf[1 + i] = '\0';
    return 0;
  }
  size_t i = 0;
  while (cur->cwd[i] && i < bufsz - 2) {
    buf[i] = cur->cwd[i];
    i++;
  }
  if (cur->cwd[i] != '\0')
    return -ENAMETOOLONG;
  if (i == 0 || buf[i - 1] != '/') {
    if (i >= bufsz - 2)
      return -ENAMETOOLONG;
    buf[i++] = '/';
  }
  size_t j = 0;
  while (path[j] && i < bufsz - 1) {
    buf[i++] = path[j++];
  }
  if (path[j] != '\0')
    return -ENAMETOOLONG;
  buf[i] = '\0';
  return 0;
}

int32_t vfs_mkdir(const char *pathname, mode_t mode) {
  if (!pathname || pathname[0] != '/')
    return -EINVAL;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t parent_idx = 0;
  const char *leaf = NULL;
  int rc = vfs_resolve_parent_and_leaf(pathname, &mnt, &parent_idx, &leaf);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->mkdir) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int crc = mnt->ops->mkdir(mnt->fs_priv, parent_idx, leaf, mode, NULL);
  spin_unlock(&vfs_lock);
  return crc;
}

int vfs_unlink(const char *path) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t parent_idx = 0;
  const char *leaf = NULL;
  int rc = vfs_resolve_parent_and_leaf(abs, &mnt, &parent_idx, &leaf);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->unlink) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int urc = mnt->ops->unlink(mnt->fs_priv, parent_idx, leaf);
  spin_unlock(&vfs_lock);

  if (urc == 0)
    dcache_invalidate(path);
  return urc;
}

int vfs_rename(const char *oldpath, const char *newpath) {
  if (!oldpath || !newpath)
    return -EINVAL;

  char old_abs[512], new_abs[512];
  int rc = make_abs_path(oldpath, old_abs, sizeof(old_abs));
  if (rc < 0)
    return rc;
  rc = make_abs_path(newpath, new_abs, sizeof(new_abs));
  if (rc < 0)
    return rc;

  spin_lock(&vfs_lock);
  mount_t *mnt_old = NULL, *mnt_new = NULL;
  uint32_t old_parent = 0, new_parent = 0;
  const char *old_leaf = NULL, *new_leaf = NULL;
  rc = vfs_resolve_parent_and_leaf(old_abs, &mnt_old, &old_parent, &old_leaf);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  rc = vfs_resolve_parent_and_leaf(new_abs, &mnt_new, &new_parent, &new_leaf);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (mnt_old != mnt_new) {
    spin_unlock(&vfs_lock);
    return -EXDEV;
  }
  if (!mnt_old->ops->rename) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int rrc = mnt_old->ops->rename(mnt_old->fs_priv, old_parent, old_leaf,
                                 new_parent, new_leaf);
  spin_unlock(&vfs_lock);

  if (rrc == 0) {
    dcache_invalidate(oldpath);
    dcache_invalidate(newpath);
  }
  return rrc;
}

int vfs_rmdir(const char *path) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t parent_idx = 0;
  const char *leaf = NULL;
  int rc = vfs_resolve_parent_and_leaf(abs, &mnt, &parent_idx, &leaf);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->rmdir) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  /* The dispatcher pre-checks non-empty: query a single dirent. */
  uint32_t child_ino = 0;
  if (mnt->ops->resolve)
    child_ino = mnt->ops->resolve(mnt->fs_priv, parent_idx, leaf);
  if (child_ino == 0) {
    spin_unlock(&vfs_lock);
    return -ENOENT;
  }
  if (mnt->ops->readdir) {
    uint8_t scratch[64];
    uint32_t pos = 0;
    ssize_t n = mnt->ops->readdir(mnt->fs_priv, child_ino, scratch,
                                  sizeof(scratch), &pos);
    if (n > 0) {
      spin_unlock(&vfs_lock);
      return -ENOTEMPTY;
    }
  }
  int rrc = mnt->ops->rmdir(mnt->fs_priv, parent_idx, leaf);
  spin_unlock(&vfs_lock);

  if (rrc == 0)
    dcache_invalidate(path);
  return rrc;
}

int vfs_truncate(const char *path, uint32_t length) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t inode_idx = 0;
  int rc = vfs_resolve_path(abs, &mnt, &inode_idx);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->truncate) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int result = mnt->ops->truncate(mnt->fs_priv, inode_idx, length);
  spin_unlock(&vfs_lock);
  return result;
}

int vfs_chmod(const char *path, mode_t mode) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t idx = 0;
  int rc = vfs_resolve_path(abs, &mnt, &idx);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->stat || !mnt->ops->chmod) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  struct stat st;
  if (mnt->ops->stat(mnt->fs_priv, idx, &st) < 0) {
    spin_unlock(&vfs_lock);
    return -ENOENT;
  }
  process_t *cur = get_current_process();
  if (cur && cur->euid != 0 && cur->euid != st.st_uid) {
    spin_unlock(&vfs_lock);
    return -EPERM;
  }
  int result = mnt->ops->chmod(mnt->fs_priv, idx, mode & 07777);
  spin_unlock(&vfs_lock);
  return result;
}

int vfs_chown(const char *path, uid_t uid, gid_t gid) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t idx = 0;
  int rc = vfs_resolve_path(abs, &mnt, &idx);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->chown) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  process_t *cur = get_current_process();
  if (cur && cur->euid != 0) {
    spin_unlock(&vfs_lock);
    return -EPERM;
  }
  int result = mnt->ops->chown(mnt->fs_priv, idx, uid, gid);
  spin_unlock(&vfs_lock);
  return result;
}

int vfs_chdir(const char *path) {
  if (!path)
    return -EINVAL;

  char abs[512];
  int aprc = make_abs_path(path, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);
  mount_t *mnt = NULL;
  uint32_t idx = 0;
  int rc = vfs_resolve_path(abs, &mnt, &idx);
  if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }
  if (!mnt->ops->stat) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  struct stat st;
  if (mnt->ops->stat(mnt->fs_priv, idx, &st) < 0) {
    spin_unlock(&vfs_lock);
    return -ENOENT;
  }
  if ((st.st_mode & 0170000) != 0040000) {
    spin_unlock(&vfs_lock);
    return -ENOTDIR;
  }
  spin_unlock(&vfs_lock);

  process_t *cur = get_current_process();
  if (!cur)
    return -ESRCH;
  kstrncpy(cur->cwd, abs, sizeof(cur->cwd));
  cur->cwd[sizeof(cur->cwd) - 1] = '\0';
  return 0;
}
