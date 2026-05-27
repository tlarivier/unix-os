/*
 * ext2_ops.c — fs_ops_t adapter glue between the VFS dispatcher and the
 * ext2 driver. Each method is a thin wrapper around an ext2 public
 * helper; fs_priv is unused today because only one ext2 mount exists,
 * but the contract carries it so a future multi-mount path needs no
 * surgery here.
 *
 * Invariants:
 *  - VFS dispatcher calls these with vfs_lock held; we never re-take
 *    vfs_lock and never call schedule()/sleep.
 *  - All inode IDs we receive and return are raw ext2 inode numbers,
 *    with 0 reserved as the not-found sentinel (matches fs_internal.h).
 *  - Wave-4 wires write/create/mkdir/truncate/unlink through new
 *    journal-backed helpers in ext2.c. rmdir/chmod/chown/rename are
 *    still -EROFS — they need an emptiness check / additional inode
 *    field plumbing that the helpers don't expose yet.
 *
 * Not allowed:
 *  - Touching on-disk ext2 layout — go through ext2.h helpers only.
 *  - Re-entering the VFS layer from any callback.
 */

#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/fs_internal.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define EXT2_OPS_S_IFREG 0x8000u
#define EXT2_OPS_S_IFDIR 0x4000u

static uint32_t ext2_op_resolve(void *fs_priv, uint32_t parent,
                                const char *name) {
  (void)fs_priv;
  return ext2_dir_lookup(parent, name);
}

static ssize_t ext2_op_read(void *fs_priv, uint32_t inode, void *buf,
                            size_t count, uint32_t offset) {
  (void)fs_priv;
  return ext2_read_inode(inode, buf, (uint32_t)count, offset);
}

static ssize_t ext2_op_write(void *fs_priv, uint32_t inode, const void *buf,
                             size_t count, uint32_t offset) {
  (void)fs_priv;
  return ext2_inode_write_data(inode, buf, (uint32_t)count, offset);
}

static int ext2_op_stat(void *fs_priv, uint32_t inode, struct stat *st) {
  (void)fs_priv;
  return ext2_stat(inode, st);
}

static ssize_t ext2_op_readdir(void *fs_priv, uint32_t inode, void *buf,
                               size_t size, uint32_t *pos_out) {
  (void)fs_priv;
  return ext2_readdir_chunk(inode, buf, size, pos_out);
}

static int ext2_op_mkdir(void *fs_priv, uint32_t parent, const char *name,
                         uint32_t mode, uint32_t *out_inode) {
  (void)fs_priv;
  uint32_t ino = 0;
  int rc =
      ext2_alloc_inode((uint16_t)((mode & 0xFFFu) | EXT2_OPS_S_IFDIR), &ino);
  if (rc != 0)
    return rc;

  rc = ext2_dir_add_entry(ino, ".", ino, EXT2_FT_DIR);
  if (rc != 0)
    return rc;
  rc = ext2_dir_add_entry(ino, "..", parent, EXT2_FT_DIR);
  if (rc != 0)
    return rc;

  rc = ext2_dir_add_entry(parent, name, ino, EXT2_FT_DIR);
  if (rc != 0)
    return rc;

  ext2_inode_adjust_links(ino, +1);
  ext2_inode_adjust_links(parent, +1);

  if (out_inode)
    *out_inode = ino;
  return 0;
}

static int ext2_op_create(void *fs_priv, uint32_t parent, const char *name,
                          uint32_t mode, uint32_t *out_inode) {
  (void)fs_priv;
  uint32_t ino = 0;
  int rc =
      ext2_alloc_inode((uint16_t)((mode & 0xFFFu) | EXT2_OPS_S_IFREG), &ino);
  if (rc != 0)
    return rc;
  rc = ext2_dir_add_entry(parent, name, ino, EXT2_FT_REG_FILE);
  if (rc != 0)
    return rc;
  if (out_inode)
    *out_inode = ino;
  return 0;
}

/* TODO: full unlink also has to drop the inode bitmap bit and
 * free the file's data blocks. */
static int ext2_op_unlink(void *fs_priv, uint32_t parent, const char *name) {
  (void)fs_priv;
  return ext2_dir_remove_entry(parent, name);
}

/* TODO: rmdir mirrors unlink with an emptiness check and
 * parent nlink decrement. Out of scope for wave-4. */
static int ext2_op_rmdir(void *fs_priv, uint32_t parent, const char *name) {
  (void)fs_priv;
  (void)parent;
  (void)name;
  return -EROFS;
}

static int ext2_op_truncate(void *fs_priv, uint32_t inode, uint32_t length) {
  (void)fs_priv;
  return ext2_inode_truncate(inode, length);
}

static int ext2_op_chmod(void *fs_priv, uint32_t inode, uint32_t mode) {
  (void)fs_priv;
  (void)inode;
  (void)mode;
  return -EROFS;
}

static int ext2_op_chown(void *fs_priv, uint32_t inode, uint32_t uid,
                         uint32_t gid) {
  (void)fs_priv;
  (void)inode;
  (void)uid;
  (void)gid;
  return -EROFS;
}

static int ext2_op_rename(void *fs_priv, uint32_t old_parent,
                          const char *old_name, uint32_t new_parent,
                          const char *new_name) {
  (void)fs_priv;
  (void)old_parent;
  (void)old_name;
  (void)new_parent;
  (void)new_name;
  return -EROFS;
}

static uint8_t ext2_op_dtype(void *fs_priv, uint32_t inode) {
  (void)fs_priv;
  return ext2_inode_dtype(inode);
}

const fs_ops_t ext2_ops = {
    .resolve = ext2_op_resolve,
    .read = ext2_op_read,
    .write = ext2_op_write,
    .stat = ext2_op_stat,
    .readdir = ext2_op_readdir,
    .mkdir = ext2_op_mkdir,
    .create = ext2_op_create,
    .unlink = ext2_op_unlink,
    .rmdir = ext2_op_rmdir,
    .truncate = ext2_op_truncate,
    .chmod = ext2_op_chmod,
    .chown = ext2_op_chown,
    .rename = ext2_op_rename,
    .dtype = ext2_op_dtype,
    .inc_open = NULL,
    .dec_open = NULL,
    .get_open = NULL,
    .release = NULL,
};
