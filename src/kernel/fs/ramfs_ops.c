/*
 * ramfs_ops.c — fs_ops_t adapter wrapping the existing ramfs primitives so
 * the VFS dispatcher (wave 3) can reach ramfs through the same vtable it
 * uses for ext2. Each method is a thin static wrapper over a single
 * ramfs_* call; the struct ramfs_inode_t stays out of vfs_fd/vfs_path.
 *
 * Invariants:
 *  - vfs_lock is held by the caller (per fs_internal.h). Adapters must
 *    not re-take it and must not sleep.
 *  - `fs_priv` is unused: ramfs is a singleton, but the parameter is
 *    kept to match the fs_ops_t signature and let a second mount install
 *    cleanly later.
 *  - inode 0 stays the cross-fs "not found" sentinel; resolve() returns
 *    it on miss and the dispatcher treats it as -ENOENT.
 *  - readdir uses `*pos_out` as the child-array cursor (matches today's
 *    vfs_readdir_fd loop).
 *
 * Not allowed:
 *  - Modifying ramfs.c semantics; we only call into its public API
 *    (plus the three minimal setters added for chmod/chown/rename).
 *  - Implementing permission policy here — euid checks remain in
 *    vfs_path.c so the adapter stays purely mechanical.
 */

#include <kernel/errno.h>
#include <kernel/fs_internal.h>
#include <kernel/kstring.h>
#include <kernel/ramfs.h>
#include <kernel/vfs.h>

static uint32_t ramfs_op_resolve(void *fs_priv, uint32_t parent,
                                 const char *name) {
  (void)fs_priv;
  if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
    ramfs_inode_t *p = ramfs_get_inode(parent);
    return p ? p->parent : 0;
  }
  return ramfs_lookup_child(parent, name);
}

static ssize_t ramfs_op_read(void *fs_priv, uint32_t inode, void *buf,
                             size_t count, uint32_t offset) {
  (void)fs_priv;
  return ramfs_read_data(inode, buf, count, offset);
}

static ssize_t ramfs_op_write(void *fs_priv, uint32_t inode, const void *buf,
                              size_t count, uint32_t offset) {
  (void)fs_priv;
  return ramfs_write_data(inode, buf, count, offset);
}

static int ramfs_op_stat(void *fs_priv, uint32_t inode, struct stat *st) {
  (void)fs_priv;
  return ramfs_fill_stat(inode, st) < 0 ? -ENOENT : 0;
}

static ssize_t ramfs_op_readdir(void *fs_priv, uint32_t inode, void *buf,
                                size_t size, uint32_t *pos_out) {
  (void)fs_priv;
  ramfs_inode_t *dir = ramfs_get_inode(inode);
  if (!dir || dir->type != RAMFS_INODE_DIR)
    return -ENOTDIR;
  if (!pos_out)
    return -EINVAL;

  uint8_t *out = (uint8_t *)buf;
  size_t written = 0;
  uint32_t pos = *pos_out;

  for (uint32_t i = pos; i < dir->child_count; i++) {
    uint32_t child = dir->children[i];
    ramfs_inode_t *child_inode = ramfs_get_inode(child);

    size_t name_len = kstrlen(child_inode->name);
    size_t reclen =
        __builtin_offsetof(struct linux_dirent, d_name) + name_len + 1;
    reclen = (reclen + 3) & ~3;

    if (written + reclen > size)
      break;

    struct linux_dirent *ent = (struct linux_dirent *)(out + written);
    ent->d_ino = child;
    ent->d_off = i + 1;
    ent->d_reclen = (uint16_t)reclen;
    kmemcpy(ent->d_name, child_inode->name, name_len + 1);

    written += reclen;
    *pos_out = i + 1;
  }

  return (ssize_t)written;
}

static int ramfs_op_mkdir(void *fs_priv, uint32_t parent, const char *name,
                          uint32_t mode, uint32_t *out_inode) {
  (void)fs_priv;
  return ramfs_create_dir(parent, name, mode, out_inode);
}

static int ramfs_op_create(void *fs_priv, uint32_t parent, const char *name,
                           uint32_t mode, uint32_t *out_inode) {
  (void)fs_priv;
  return ramfs_create_file(parent, name, mode, out_inode);
}

static int ramfs_op_unlink(void *fs_priv, uint32_t parent, const char *name) {
  (void)fs_priv;
  uint32_t idx = ramfs_lookup_child(parent, name);
  if (idx == 0)
    return -ENOENT;

  ramfs_inode_t *inode = ramfs_get_inode(idx);
  if (!inode)
    return -ENOENT;
  if (inode->type == RAMFS_INODE_DIR)
    return -EISDIR;

  ramfs_remove_child(parent, idx);
  if (inode->nlink > 0)
    inode->nlink--;
  if (inode->nlink == 0 && ramfs_get_open_count(idx) == 0)
    ramfs_free_inode(idx);
  return 0;
}

static int ramfs_op_rmdir(void *fs_priv, uint32_t parent, const char *name) {
  (void)fs_priv;
  uint32_t idx = ramfs_lookup_child(parent, name);
  if (idx == 0)
    return -ENOENT;

  ramfs_inode_t *dir = ramfs_get_inode(idx);
  if (!dir)
    return -ENOENT;
  if (dir->type != RAMFS_INODE_DIR)
    return -ENOTDIR;
  if (dir->child_count > 0)
    return -ENOTEMPTY;

  ramfs_remove_child(parent, idx);
  ramfs_inode_t *parent_inode = ramfs_get_inode(parent);
  if (parent_inode && parent_inode->nlink > 0)
    parent_inode->nlink--;
  ramfs_free_inode(idx);
  return 0;
}

static int ramfs_op_truncate(void *fs_priv, uint32_t inode, uint32_t length) {
  (void)fs_priv;
  return ramfs_truncate_inode(inode, length);
}

static int ramfs_op_chmod(void *fs_priv, uint32_t inode, uint32_t mode) {
  (void)fs_priv;
  return ramfs_set_mode(inode, mode);
}

static int ramfs_op_chown(void *fs_priv, uint32_t inode, uint32_t uid,
                          uint32_t gid) {
  (void)fs_priv;
  return ramfs_set_owner(inode, uid, gid);
}

static int ramfs_op_rename(void *fs_priv, uint32_t old_parent,
                           const char *old_name, uint32_t new_parent,
                           const char *new_name) {
  (void)fs_priv;
  uint32_t idx = ramfs_lookup_child(old_parent, old_name);
  if (idx == 0)
    return -ENOENT;

  int rc = ramfs_remove_child(old_parent, idx);
  if (rc < 0)
    return rc;
  ramfs_set_name(idx, new_name);
  rc = ramfs_add_child(new_parent, idx);
  if (rc < 0) {
    ramfs_add_child(old_parent, idx);
    return rc;
  }
  return 0;
}

static uint8_t ramfs_op_dtype(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  return ramfs_inode_dtype(inode);
}

static void ramfs_op_inc_open(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  ramfs_inc_open_count(inode);
}

static void ramfs_op_dec_open(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  ramfs_dec_open_count(inode);
}

static uint32_t ramfs_op_get_open(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  return ramfs_get_open_count(inode);
}

static void ramfs_op_release(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  ramfs_free_inode(inode);
}

const fs_ops_t ramfs_ops = {
    .resolve = ramfs_op_resolve,
    .read = ramfs_op_read,
    .write = ramfs_op_write,
    .stat = ramfs_op_stat,
    .readdir = ramfs_op_readdir,
    .mkdir = ramfs_op_mkdir,
    .create = ramfs_op_create,
    .unlink = ramfs_op_unlink,
    .rmdir = ramfs_op_rmdir,
    .truncate = ramfs_op_truncate,
    .chmod = ramfs_op_chmod,
    .chown = ramfs_op_chown,
    .rename = ramfs_op_rename,
    .dtype = ramfs_op_dtype,
    .inc_open = ramfs_op_inc_open,
    .dec_open = ramfs_op_dec_open,
    .get_open = ramfs_op_get_open,
    .release = ramfs_op_release,
};
