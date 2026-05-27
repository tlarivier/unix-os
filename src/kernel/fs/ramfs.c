/*
 * ramfs.c — In-memory inode store backing the VFS: fixed 128-slot table,
 * parent/child links, contiguous kmalloc-backed data, plus POSIX
 * stat/getdents helpers consumed exclusively by vfs_core/fd/path.
 *
 * Invariants:
 *  - vfs_lock (held by callers in vfs_*.c) serializes every mutation of
 *    inode_table[] and inode_open_count[].
 *  - inode 0 is permanently reserved as the "not found" sentinel; root
 *    lives at inode 1; ramfs_alloc_inode starts at 2.
 *  - inode_open_count[] is independent from nlink: free only when both
 *    nlink == 0 and open_count == 0.
 *  - Data buffers are owned by the inode and freed on truncate/free.
 *
 * Not allowed:
 *  - Exposing ramfs_inode_t / inode_table[] outside kernel/fs/.
 *  - Calling kmalloc/kfree from IRQ context.
 *  - Taking vfs_lock here (caller already owns it).
 */

#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/ramfs.h>
#include <kernel/vfs.h>

#ifndef S_IFDIR
#define S_IFDIR 0040000
#define S_IFREG 0100000
#endif

static ramfs_inode_t inode_table[RAMFS_MAX_INODES];
static uint32_t inode_open_count[RAMFS_MAX_INODES];

uint32_t ramfs_get_open_count(uint32_t idx) { return inode_open_count[idx]; }
void ramfs_inc_open_count(uint32_t idx) { inode_open_count[idx]++; }
void ramfs_dec_open_count(uint32_t idx) { inode_open_count[idx]--; }

#define RAMFS_ROOT_INO 1u

static uint32_t ramfs_alloc_inode(void) {
  for (uint32_t i = 2; i < RAMFS_MAX_INODES; i++) {
    if (inode_table[i].type == RAMFS_INODE_FREE) {
      kmemset(&inode_table[i], 0, sizeof(ramfs_inode_t));
      inode_table[i].nlink = 1;
      return i;
    }
  }
  return 0;
}

void ramfs_free_inode(uint32_t idx) {
  if (idx == 0 || idx >= RAMFS_MAX_INODES)
    return;

  ramfs_inode_t *node = &inode_table[idx];
  if (node->data) {
    kfree(node->data);
    node->data = NULL;
  }
  kmemset(node, 0, sizeof(ramfs_inode_t));
  inode_open_count[idx] = 0;
}

ramfs_inode_t *ramfs_get_inode(uint32_t idx) { return &inode_table[idx]; }

uint32_t ramfs_lookup_child(uint32_t parent, const char *name) {
  ramfs_inode_t *pnode = &inode_table[parent];

  if (pnode->type != RAMFS_INODE_DIR)
    return 0;

  for (uint32_t i = 0; i < pnode->child_count; i++) {
    uint32_t child = pnode->children[i];
    if (kstrcmp(inode_table[child].name, name) == 0) {
      return child;
    }
  }
  return 0;
}

int ramfs_add_child(uint32_t parent, uint32_t child) {
  ramfs_inode_t *pnode = &inode_table[parent];
  if (pnode->type != RAMFS_INODE_DIR)
    return -ENOTDIR;
  if (pnode->child_count >= RAMFS_MAX_CHILDREN)
    return -ENOSPC;

  pnode->children[pnode->child_count++] = child;
  inode_table[child].parent = parent;
  return 0;
}

int ramfs_remove_child(uint32_t parent, uint32_t child) {
  ramfs_inode_t *pnode = &inode_table[parent];
  if (pnode->type != RAMFS_INODE_DIR)
    return -ENOTDIR;

  for (uint32_t i = 0; i < pnode->child_count; i++) {
    if (pnode->children[i] == child) {
      for (uint32_t j = i; j < pnode->child_count - 1; j++) {
        pnode->children[j] = pnode->children[j + 1];
      }
      pnode->child_count--;
      return 0;
    }
  }
  return -ENOENT;
}

ssize_t ramfs_read_data(uint32_t inode_idx, void *buf, size_t count,
                        uint32_t offset) {
  ramfs_inode_t *inode = &inode_table[inode_idx];
  if (inode->type != RAMFS_INODE_FILE)
    return -EISDIR;

  if (offset >= inode->size)
    return 0;

  uint32_t available = inode->size - offset;
  uint32_t to_read = (count < available) ? count : available;

  if (to_read == 0)
    return 0;

  if (inode->data) {
    kmemcpy(buf, inode->data + offset, to_read);
  }

  return (ssize_t)to_read;
}

ssize_t ramfs_write_data(uint32_t inode_idx, const void *buf, size_t count,
                         uint32_t offset) {
  ramfs_inode_t *inode = &inode_table[inode_idx];
  if (inode->type == RAMFS_INODE_DIR)
    return -EISDIR;

  if (count > UINT32_MAX - offset)
    return -EFBIG;
  uint32_t end_pos = offset + count;
  if (end_pos > RAMFS_MAX_FILE_SIZE)
    return -EFBIG;

  if (end_pos > inode->data_capacity) {
    uint32_t new_cap =
        (end_pos + RAMFS_BLOCK_SIZE - 1) & ~(RAMFS_BLOCK_SIZE - 1);
    if (new_cap > RAMFS_MAX_FILE_SIZE)
      new_cap = RAMFS_MAX_FILE_SIZE;

    uint8_t *new_data = kmalloc(new_cap);
    if (!new_data) {
      return -ENOSPC;
    }

    kmemset(new_data, 0, new_cap);
    if (inode->data && inode->size > 0) {
      kmemcpy(new_data, inode->data, inode->size);
      kfree(inode->data);
    }
    inode->data = new_data;
    inode->data_capacity = new_cap;
  }

  kmemcpy(inode->data + offset, buf, count);
  if (end_pos > inode->size)
    inode->size = end_pos;
  return (ssize_t)count;
}

int ramfs_truncate_inode(uint32_t inode_idx, uint32_t length) {
  ramfs_inode_t *inode = &inode_table[inode_idx];
  if (inode->type != RAMFS_INODE_FILE)
    return -EISDIR;
  if (length > RAMFS_MAX_FILE_SIZE)
    return -EFBIG;

  if (length > inode->data_capacity) {
    uint32_t new_cap =
        (length + RAMFS_BLOCK_SIZE - 1) & ~(RAMFS_BLOCK_SIZE - 1);
    uint8_t *new_data = kmalloc(new_cap);
    if (!new_data)
      return -ENOSPC;

    kmemset(new_data, 0, new_cap);
    if (inode->data && inode->size > 0) {
      uint32_t copy_size = (inode->size < length) ? inode->size : length;
      kmemcpy(new_data, inode->data, copy_size);
      kfree(inode->data);
    }
    inode->data = new_data;
    inode->data_capacity = new_cap;
  }

  inode->size = length;
  return 0;
}

int ramfs_init(void) {
  kmemset(inode_table, 0, sizeof(inode_table));
  kmemset(inode_open_count, 0, sizeof(inode_open_count));

  inode_table[RAMFS_ROOT_INO].type = RAMFS_INODE_DIR;
  inode_table[RAMFS_ROOT_INO].mode = 0755;
  inode_table[RAMFS_ROOT_INO].nlink = 2;
  kstrcpy(inode_table[RAMFS_ROOT_INO].name, "/");

  return 0;
}

int ramfs_create_dir(uint32_t parent_idx, const char *name, uint32_t mode,
                     uint32_t *out_idx) {
  ramfs_inode_t *parent = ramfs_get_inode(parent_idx);
  if (!parent || parent->type != RAMFS_INODE_DIR)
    return -ENOTDIR;
  if (ramfs_lookup_child(parent_idx, name) != 0)
    return -EEXIST;

  uint32_t new_idx = ramfs_alloc_inode();
  if (new_idx == 0)
    return -ENOSPC;

  ramfs_inode_t *dir = &inode_table[new_idx];
  dir->type = RAMFS_INODE_DIR;
  dir->mode = mode & 0777;
  dir->nlink = 2;
  kstrncpy(dir->name, name, RAMFS_MAX_NAME);

  int rc = ramfs_add_child(parent_idx, new_idx);
  if (rc < 0) {
    ramfs_free_inode(new_idx);
    return rc;
  }

  parent->nlink++;
  if (out_idx)
    *out_idx = new_idx;
  return 0;
}

int ramfs_create_file(uint32_t parent_idx, const char *name, uint32_t mode,
                      uint32_t *out_idx) {
  ramfs_inode_t *parent = ramfs_get_inode(parent_idx);
  if (!parent || parent->type != RAMFS_INODE_DIR)
    return -ENOTDIR;

  uint32_t new_idx = ramfs_alloc_inode();
  if (new_idx == 0)
    return -ENOSPC;

  ramfs_inode_t *file = &inode_table[new_idx];
  file->type = RAMFS_INODE_FILE;
  file->mode = mode & 0777;
  file->nlink = 1;
  kstrncpy(file->name, name, RAMFS_MAX_NAME);

  int rc = ramfs_add_child(parent_idx, new_idx);
  if (rc < 0) {
    ramfs_free_inode(new_idx);
    return rc;
  }

  if (out_idx)
    *out_idx = new_idx;
  return 0;
}

int ramfs_fill_stat(uint32_t inode_idx, struct stat *st) {
  ramfs_inode_t *inode = &inode_table[inode_idx];
  if (inode->type == RAMFS_INODE_FREE)
    return -1;

  kmemset(st, 0, sizeof(struct stat));
  st->st_ino = inode_idx;
  st->st_mode = inode->mode;
  st->st_mode |= (inode->type == RAMFS_INODE_DIR) ? S_IFDIR : S_IFREG;
  st->st_nlink = inode->nlink;
  st->st_size = inode->size;
  st->st_blksize = RAMFS_BLOCK_SIZE;
  st->st_blocks = (inode->size + 511) / 512;
  return 0;
}

uint8_t ramfs_inode_dtype(uint32_t inode_idx) {
  ramfs_inode_t *node = &inode_table[inode_idx];
  if (node->type == RAMFS_INODE_DIR)
    return 4;
  if (node->type == RAMFS_INODE_FILE)
    return 8;
  return 0; /* DT_UNKNOWN */
}

int ramfs_set_mode(uint32_t idx, uint32_t mode) {
  if (idx == 0 || idx >= RAMFS_MAX_INODES)
    return -ENOENT;
  ramfs_inode_t *node = &inode_table[idx];
  if (node->type == RAMFS_INODE_FREE)
    return -ENOENT;
  node->mode = mode & 07777;
  return 0;
}

int ramfs_set_owner(uint32_t idx, uint32_t uid, uint32_t gid) {
  if (idx == 0 || idx >= RAMFS_MAX_INODES)
    return -ENOENT;
  ramfs_inode_t *node = &inode_table[idx];
  if (node->type == RAMFS_INODE_FREE)
    return -ENOENT;
  if (uid != (uint32_t)-1)
    node->uid = uid;
  if (gid != (uint32_t)-1)
    node->gid = gid;
  return 0;
}

uint32_t ramfs_root_inode(void) { return RAMFS_ROOT_INO; }

int ramfs_set_name(uint32_t idx, const char *name) {
  if (idx == 0 || idx >= RAMFS_MAX_INODES || !name)
    return -ENOENT;
  ramfs_inode_t *node = &inode_table[idx];
  if (node->type == RAMFS_INODE_FREE)
    return -ENOENT;
  kstrncpy(node->name, name, RAMFS_MAX_NAME);
  node->name[RAMFS_MAX_NAME - 1] = '\0';
  return 0;
}
