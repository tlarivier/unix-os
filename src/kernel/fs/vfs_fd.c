/*
 * vfs_fd.c — fd-keyed VFS operations: open/close/dup/dup2, read/write/
 * lseek/pread, stat/fstat, readdir_fd, ftruncate; also owns the boot-time
 * kernel_fd_table[] used before init_proc exists.
 *
 * Invariants:
 *  - vfs_lock is held across every mutation of open_files[] and every
 *    fs inode update (create/truncate/write/free).
 *  - proc_fd_t.of_idx is encoded as raw_idx + 1; 0 means empty slot. All
 *    decoding goes through of_idx_decode().
 *  - inode 0 is the cross-fs "not found" sentinel. Each open_files slot
 *    caches the originating fs_ops vtable + fs_priv so I/O dispatches
 *    through the mount that originally resolved the path.
 *  - fd ranges: process fd_table when get_current_process() != NULL,
 *    else kernel_fd_table for boot callers.
 *
 * Not allowed:
 *  - Calling schedule() (or synchronize_rcu) while holding vfs_lock.
 *  - Reaching into a concrete fs's internals from this TU — every fs
 *    interaction goes through of->fs / mnt->ops.
 *  - Invoking ext2 write-path or journal helpers directly.
 */

#include <../uapi/syscalls.h>
#include <kernel/console.h>
#include <kernel/constants.h>
#include <kernel/errno.h>
#include <kernel/fs_internal.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/process.h>
#include <kernel/ramfs.h>
#include <kernel/vfs.h>
#include <kernel/vfs_internal.h>

static proc_fd_t kernel_fd_table[VFS_MAX_OPEN_FILES];

static void __vfs_close_locked(int fd);

static proc_fd_t *fd_resolve_slot(int fd) {
  process_t *proc = get_current_process();
  proc_fd_t *t = proc ? proc->fd_table : kernel_fd_table;
  int max = proc ? MAX_OPEN_FILES_CONST : VFS_MAX_OPEN_FILES;
  if (fd < 0 || fd >= max)
    return NULL;
  return &t[fd];
}

static int fd_to_of_idx_locked(int fd) {
  proc_fd_t *slot = fd_resolve_slot(fd);
  if (!slot || slot->of_idx == 0)
    return -1;
  uint32_t raw = slot->of_idx - 1;
  if (raw >= VFS_MAX_OPEN_FILES || open_files[raw].refcount == 0)
    return -1;
  return (int)raw;
}

static int find_free_fd(void) {
  process_t *proc = get_current_process();
  proc_fd_t *t = proc ? proc->fd_table : kernel_fd_table;
  int start = proc ? 3 : 0;
  int max = proc ? MAX_OPEN_FILES_CONST : VFS_MAX_OPEN_FILES;
  for (int i = start; i < max; i++) {
    if (t[i].of_idx == 0)
      return i;
  }
  return -EMFILE;
}

#ifndef O_ACCMODE
#define O_ACCMODE 0x3
#endif
#define VFS_OPEN_SUPPORTED_FLAGS                                               \
  (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC |      \
   O_APPEND | O_NONBLOCK | O_DSYNC | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)

static int vfs_make_abs_path(const char *pathname, char *buf, size_t bufsz) {
  if (!pathname || bufsz < 2)
    return -EINVAL;
  if (pathname[0] == '/') {
    size_t i = 0;
    while (pathname[i] && i < bufsz - 1) {
      buf[i] = pathname[i];
      i++;
    }
    if (pathname[i] != '\0')
      return -ENAMETOOLONG;
    buf[i] = '\0';
    return 0;
  }
  process_t *cur = get_current_process();
  if (!cur || cur->cwd[0] == '\0') {
    buf[0] = '/';
    size_t i = 0;
    while (pathname[i] && i < bufsz - 2) {
      buf[1 + i] = pathname[i];
      i++;
    }
    if (pathname[i] != '\0')
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
  while (pathname[j] && i < bufsz - 1) {
    buf[i++] = pathname[j++];
  }
  if (pathname[j] != '\0')
    return -ENAMETOOLONG;
  buf[i] = '\0';
  return 0;
}

int32_t vfs_open(const char *pathname, int flags, mode_t mode) {
  if (!pathname)
    return -EINVAL;

  if ((unsigned)flags & ~(unsigned)VFS_OPEN_SUPPORTED_FLAGS)
    return -EINVAL;
  if (((unsigned)flags & O_ACCMODE) == O_ACCMODE)
    return -EINVAL;

  char abs[512];
  int aprc = vfs_make_abs_path(pathname, abs, sizeof(abs));
  if (aprc < 0)
    return aprc;

  spin_lock(&vfs_lock);

  mount_t *mnt = NULL;
  uint32_t inode_idx = 0;
  int rc = vfs_resolve_path(abs, &mnt, &inode_idx);

  if (rc == -ENOENT && (flags & O_CREAT)) {
    const char *leaf = NULL;
    uint32_t parent_idx = 0;
    int prc = vfs_resolve_parent_and_leaf(abs, &mnt, &parent_idx, &leaf);
    if (prc < 0) {
      spin_unlock(&vfs_lock);
      return prc;
    }
    if (!mnt->ops->create) {
      spin_unlock(&vfs_lock);
      return -ENOSYS;
    }
    int crc =
        mnt->ops->create(mnt->fs_priv, parent_idx, leaf, mode, &inode_idx);
    if (crc < 0) {
      spin_unlock(&vfs_lock);
      return crc;
    }
  } else if (rc < 0) {
    spin_unlock(&vfs_lock);
    return rc;
  }

  if ((flags & O_TRUNC) && mnt->ops->truncate)
    mnt->ops->truncate(mnt->fs_priv, inode_idx, 0);

  int of_idx = vfs_alloc_open_file();
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EMFILE;
  }

  uint32_t start_pos = 0;
  if (flags & O_APPEND) {
    struct stat st;
    if (mnt->ops->stat && mnt->ops->stat(mnt->fs_priv, inode_idx, &st) == 0)
      start_pos = st.st_size;
  }

  open_files[of_idx].inode = inode_idx;
  open_files[of_idx].pos = start_pos;
  open_files[of_idx].flags = flags;
  open_files[of_idx].fs = mnt->ops;
  open_files[of_idx].fs_priv = mnt->fs_priv;

  int fd = find_free_fd();
  if (fd < 0) {
    vfs_free_open_file(of_idx);
    spin_unlock(&vfs_lock);
    return -EMFILE;
  }
  proc_fd_t *slot = fd_resolve_slot(fd);
  slot->of_idx = of_idx + 1;
  slot->flags = flags;
  slot->offset = 0;
  slot->refcount = 1;

  if (mnt->ops->inc_open)
    mnt->ops->inc_open(mnt->fs_priv, inode_idx);

#ifdef DEBUG_VFS
  kprintf("vfs_open: path=%s fd=%d of_idx=%d\n", pathname, fd, of_idx);
#endif
  spin_unlock(&vfs_lock);
  return fd;
}

static void __vfs_close_locked(int fd) {
  if (fd < 0)
    return;
  proc_fd_t *slot = fd_resolve_slot(fd);
  if (!slot || slot->of_idx == 0)
    return;

  uint32_t of_idx = slot->of_idx - 1;
  if (of_idx < VFS_MAX_OPEN_FILES && open_files[of_idx].refcount > 0) {
    open_files[of_idx].refcount--;
    if (open_files[of_idx].refcount == 0) {
      vfs_open_file_t *of = &open_files[of_idx];
      const fs_ops_t *fs = of->fs;
      void *fs_priv = of->fs_priv;
      uint32_t inode_idx = of->inode;
      if (fs) {
        if (fs->dec_open)
          fs->dec_open(fs_priv, inode_idx);
        if (fs->get_open && fs->release &&
            fs->get_open(fs_priv, inode_idx) == 0) {
          struct stat st;
          if (fs->stat && fs->stat(fs_priv, inode_idx, &st) == 0 &&
              st.st_nlink == 0) {
            fs->release(fs_priv, inode_idx);
          }
        }
      }
      vfs_free_open_file(of_idx);
    }
  }

  kmemset(slot, 0, sizeof(*slot));
}

void vfs_close(int fd) {
  if (fd < 0)
    return;
  spin_lock(&vfs_lock);
  __vfs_close_locked(fd);
  spin_unlock(&vfs_lock);
}

ssize_t vfs_read(int fd, void *buf, size_t count) {
  if (!buf)
    return -EINVAL;

  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (((unsigned)of->flags & O_ACCMODE) == O_WRONLY) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }
  if (!of->fs || !of->fs->read) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  ssize_t result = of->fs->read(of->fs_priv, of->inode, buf, count, of->pos);
  if (result > 0)
    of->pos += result;

  spin_unlock(&vfs_lock);
  return result;
}

ssize_t vfs_write(int fd, const void *buf, size_t count) {
  if (!buf)
    return -EINVAL;
  if (count == 0)
    return 0;

  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (((unsigned)of->flags & O_ACCMODE) == O_RDONLY) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }
  if (!of->fs || !of->fs->write) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }

  if (of->flags & O_APPEND) {
    struct stat st;
    if (of->fs->stat && of->fs->stat(of->fs_priv, of->inode, &st) == 0)
      of->pos = st.st_size;
  }

  ssize_t result = of->fs->write(of->fs_priv, of->inode, buf, count, of->pos);
  if (result > 0)
    of->pos += result;

  spin_unlock(&vfs_lock);
  return result;
}

off_t vfs_lseek(int fd, off_t offset, int whence) {
  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];

  int32_t new_pos;
  switch (whence) {
  case 0: /* SEEK_SET */
    new_pos = offset;
    break;
  case 1: /* SEEK_CUR */
    new_pos = (int32_t)of->pos + offset;
    break;
  case 2: { /* SEEK_END */
    uint32_t end = 0;
    if (of->fs && of->fs->stat) {
      struct stat st;
      if (of->fs->stat(of->fs_priv, of->inode, &st) == 0)
        end = st.st_size;
    }
    new_pos = (int32_t)end + offset;
    break;
  }
  default:
    spin_unlock(&vfs_lock);
    return -EINVAL;
  }

  if (new_pos < 0) {
    spin_unlock(&vfs_lock);
    return -EINVAL;
  }

  of->pos = (uint32_t)new_pos;

  spin_unlock(&vfs_lock);
  return (off_t)new_pos;
}

int vfs_stat(const char *path, struct stat *st) {
  if (!path || !st)
    return -EINVAL;

  char abs[512];
  int aprc = vfs_make_abs_path(path, abs, sizeof(abs));
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
  if (!mnt->ops->stat) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int result = mnt->ops->stat(mnt->fs_priv, inode_idx, st);
  spin_unlock(&vfs_lock);
  return result;
}

int vfs_fstat(int fd, struct stat *st) {
  if (!st)
    return -EINVAL;

  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (!of->fs || !of->fs->stat) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int result = of->fs->stat(of->fs_priv, of->inode, st);
  spin_unlock(&vfs_lock);
  return result < 0 ? -EBADF : 0;
}

int vfs_dup(int oldfd) {
  if (oldfd < 0 || oldfd >= MAX_OPEN_FILES_CONST)
    return -EBADF;

  process_t *proc = get_current_process();
  if (!proc)
    return -ESRCH;

  spin_lock(&vfs_lock);

  uint32_t of_idx = proc->fd_table[oldfd].of_idx;
  if (of_idx == 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  int newfd = find_free_fd();
  if (newfd < 0) {
    spin_unlock(&vfs_lock);
    return -EMFILE;
  }

  proc->fd_table[newfd] = proc->fd_table[oldfd];
  open_files[of_idx - 1].refcount++;

  spin_unlock(&vfs_lock);
  return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
  if (oldfd < 0 || newfd < 0)
    return -EBADF;
  if (oldfd >= MAX_OPEN_FILES_CONST || newfd >= MAX_OPEN_FILES_CONST)
    return -EBADF;
  if (oldfd == newfd)
    return newfd;

  process_t *proc = get_current_process();
  if (!proc)
    return -ESRCH;

  spin_lock(&vfs_lock);

  uint32_t of_idx = proc->fd_table[oldfd].of_idx;
  if (of_idx == 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  if (proc->fd_table[newfd].of_idx != 0) {
    __vfs_close_locked(newfd);
  }

  proc->fd_table[newfd] = proc->fd_table[oldfd];
  open_files[of_idx - 1].refcount++;

  spin_unlock(&vfs_lock);
  return newfd;
}

ssize_t vfs_readdir_fd(int fd, void *buffer, size_t size) {
  if (!buffer)
    return -EINVAL;

  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (!of->fs || !of->fs->readdir) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  uint32_t pos = of->pos;
  ssize_t result = of->fs->readdir(of->fs_priv, of->inode, buffer, size, &pos);
  if (result >= 0)
    of->pos = pos;

  spin_unlock(&vfs_lock);
  return result;
}

int vfs_ftruncate(int fd, uint32_t length) {
  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (!of->fs || !of->fs->truncate) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  int result = of->fs->truncate(of->fs_priv, of->inode, length);
  spin_unlock(&vfs_lock);
  return result;
}

ssize_t vfs_pread(int fd, void *buf, size_t count, uint32_t offset) {
  if (!buf)
    return -EINVAL;

  spin_lock(&vfs_lock);
  int of_idx = fd_to_of_idx_locked(fd);
  if (of_idx < 0) {
    spin_unlock(&vfs_lock);
    return -EBADF;
  }

  vfs_open_file_t *of = &open_files[of_idx];
  if (!of->fs || !of->fs->read) {
    spin_unlock(&vfs_lock);
    return -ENOSYS;
  }
  ssize_t result = of->fs->read(of->fs_priv, of->inode, buf, count, offset);
  spin_unlock(&vfs_lock);
  return result;
}

vfs_fd_kind_t vfs_fd_kind(int fd, int *pipe_id_out) {
  if (fd < 0 || fd >= MAX_OPEN_FILES_CONST)
    return FD_KIND_INVALID;
  process_t *proc = get_current_process();
  if (!proc)
    return FD_KIND_INVALID;

  uint32_t of_idx = proc->fd_table[fd].of_idx;
  if (of_idx == CONSOLE_INODE_MAGIC)
    return FD_KIND_CONSOLE;
  if (of_idx >= PIPE_FD_BASE) {
    if (pipe_id_out)
      *pipe_id_out = (int)(of_idx - PIPE_FD_BASE);
    return FD_KIND_PIPE;
  }
  if (of_idx == 0)
    return FD_KIND_INVALID;
  return FD_KIND_VFS;
}

const char *vfs_get_path_by_fd(int fd) {
  if (fd < 0 || fd >= MAX_OPEN_FILES_CONST)
    return NULL;

  process_t *proc = get_current_process();
  if (!proc)
    return NULL;

  uint32_t of_idx = proc->fd_table[fd].of_idx;
  if (of_idx == 0)
    return NULL;
  of_idx--;

  if (of_idx >= VFS_MAX_OPEN_FILES)
    return NULL;

  vfs_open_file_t *of = &open_files[of_idx];
  if (of->fs != &ramfs_ops)
    return NULL;

  uint32_t inode_idx = of->inode;
  if (inode_idx >= RAMFS_MAX_INODES)
    return NULL;

  return ramfs_get_inode(inode_idx)->name;
}
