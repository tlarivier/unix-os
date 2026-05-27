/*
 * sys_fs.c — thin user<->kernel marshalling for the 20 file/FS syscalls
 * (open/close/read/write/lseek/stat/fstat/dup/dup2/mkdir/chdir/getcwd/
 * unlink/getdents/chmod/chown/rename/rmdir/truncate/ftruncate), dispatching
 * to VFS, pipe or console via vfs_fd_kind.
 *
 * Invariants:
 *  - Every wrapper has the uniform (uint32_t x5) -> int32_t syscall ABI.
 *  - User pointers are touched only through copy_{to,from,str_from}_user.
 *  - fd discrimination (console/pipe/regular) is owned by VFS, not here.
 *  - On success returns >= 0; on error returns a negative errno.
 *
 * Not allowed:
 *  - Reaching into proc->fd_table[] fields directly.
 *  - Implementing FS semantics — delegate to vfs_<X>.
 *  - Holding global mutable per-fd state in this TU.
 */

#include "syscall.h"
#include "syscall_protos.h"

#include <kernel/console.h>
#include <kernel/constants.h>
#include <kernel/errno.h>
#include <kernel/keyboard.h>
#include <kernel/kstring.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/serial.h>
#include <kernel/uaccess.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <kernel/vga.h>
#include <uapi/syscalls.h>

/* struct stat is the UAPI definition (uapi/types.h); the kernel writes it
 * directly and the syscall hands the bytes to userspace verbatim. */
static int emit_stat(uint32_t ubuf, const struct stat *kst) {
  int rc = copy_to_user((void *)ubuf, kst, sizeof(*kst));
  return IS_ERROR(rc) ? rc : 0;
}

static int copy_user_path(uint32_t upath, char *kpath) {
  if (!upath)
    return -EINVAL;
  int rc = copy_str_from_user(kpath, (const char *)upath, PATH_MAX_CONST);
  return IS_ERROR(rc) ? rc : 0;
}

static inline int is_console_device(uint32_t fd) {
  return vfs_fd_kind((int)fd, NULL) == FD_KIND_CONSOLE;
}

int32_t sys_open(uint32_t path, uint32_t flags, uint32_t mode, uint32_t u4,
                 uint32_t u5) {
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  /* mode is consulted only when O_CREAT is set; mask to permission bits. */
  return vfs_open(kpath, (int)flags, (mode_t)(mode & 0777));
}

int32_t sys_close(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  if (fd >= MAX_OPEN_FILES_CONST)
    return -EBADF;

  process_t *proc = get_current_process();
  int pipe_id;

  switch (vfs_fd_kind((int)fd, &pipe_id)) {
  case FD_KIND_CONSOLE:
    proc->fd_table[fd].of_idx = 0;
    proc->fd_table[fd].flags = 0;
    proc->fd_table[fd].offset = 0;
    proc->fd_table[fd].refcount = 0;
    return 0;
  case FD_KIND_PIPE: {
    int is_write_end = !!(proc->fd_table[fd].flags & O_WRONLY);
    proc->fd_table[fd].of_idx = 0;
    proc->fd_table[fd].flags = 0;
    return pipe_close_by_id(pipe_id, is_write_end);
  }
  case FD_KIND_VFS:
    vfs_close(fd);
    return 0;
  case FD_KIND_INVALID:
  default:
    return -EBADF;
  }
}

int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t u4,
                 uint32_t u5) {
  (void)u4;
  (void)u5;

  if (fd >= MAX_OPEN_FILES_CONST && !is_pipe_fd(fd, NULL))
    return -EBADF;
  /* Reject sizes that would let `buf + total` wrap a uint32_t — copy_to_user
   * checks individual chunks but a chunked loop would never observe that. */
  if (count > INT32_MAX || buf + count < buf)
    return -EINVAL;

  if (is_console_device(fd)) {
    char k = kb_get_char();
    int rc = copy_to_user((void *)buf, &k, 1);
    return IS_ERROR(rc) ? rc : 1;
  }

  int pipe_id;
  if (is_pipe_fd(fd, &pipe_id)) {
    return (int32_t)pipe_read_by_id(pipe_id, (void *)buf, count);
  }

  char kbuf[BUF_SIZE_CONST];
  uint32_t remaining = count;
  int32_t total = 0;

  while (remaining > 0) {
    uint32_t chunk = (remaining > BUF_SIZE_CONST) ? BUF_SIZE_CONST : remaining;
    int32_t r = vfs_read((int)fd, kbuf, chunk);
    if (r <= 0)
      break;

    int rc = copy_to_user((void *)(buf + total), kbuf, (size_t)r);
    if (IS_ERROR(rc))
      return (total > 0) ? total : rc;

    total += r;
    remaining -= (uint32_t)r;
  }
  return total;
}

int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t u4,
                  uint32_t u5) {
  (void)u4;
  (void)u5;

  if (fd >= MAX_OPEN_FILES_CONST && !is_pipe_fd(fd, NULL))
    return -EBADF;
  if (count == 0)
    return 0;
  if (count > INT32_MAX || buf + count < buf)
    return -EINVAL;

  int pipe_id;
  if (is_pipe_fd(fd, &pipe_id)) {
    return (int32_t)pipe_write_by_id(pipe_id, (const void *)buf, count);
  }

  char kbuf[BUF_SIZE_CONST];
  size_t written = 0;

  if (is_console_device(fd)) {
    while (written < count) {
      size_t chunk = (count - written > BUF_SIZE_CONST) ? BUF_SIZE_CONST
                                                        : (count - written);
      int rc = copy_from_user(kbuf, (const void *)(buf + written), chunk);
      if (rc < 0)
        return (written > 0) ? (int32_t)written : rc;

      for (size_t i = 0; i < chunk; i++) {
        vga_putchar(kbuf[i]);
        serial_putc(kbuf[i]);
      }
      written += chunk;
    }
    return (int32_t)count;
  }

  while (written < count) {
    size_t chunk =
        (count - written > BUF_SIZE_CONST) ? BUF_SIZE_CONST : (count - written);
    int rc = copy_from_user(kbuf, (const void *)(buf + written), chunk);
    if (rc < 0)
      return (written > 0) ? (int32_t)written : rc;

    int32_t w = vfs_write((int)fd, kbuf, chunk);
    if (w <= 0)
      break;
    written += (size_t)w;
  }
  return (int32_t)written;
}

int32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t u4,
                  uint32_t u5) {
  (void)u4;
  (void)u5;

  if (fd >= MAX_OPEN_FILES_CONST)
    return -EBADF;
  if (whence > 2)
    return -EINVAL;

  return (int32_t)vfs_lseek((int)fd, (off_t)offset, (int)whence);
}

int32_t sys_stat(uint32_t path, uint32_t buf, uint32_t u3, uint32_t u4,
                 uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (!buf)
    return -EINVAL;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  struct stat kst;
  rc = vfs_stat(kpath, &kst);
  if (rc < 0)
    return rc;

  return emit_stat(buf, &kst);
}

int32_t sys_fstat(uint32_t fd, uint32_t buf, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (!buf)
    return -EINVAL;

  struct stat kst;

  if (is_console_device(fd)) {
    kmemset(&kst, 0, sizeof(kst));
    kst.st_ino = CONSOLE_INODE_MAGIC;
    kst.st_mode = S_IFCHR | 0666; /* Character device, rw-rw-rw- */
    kst.st_nlink = 1;
    kst.st_rdev = 0x0501; /* Major 5, minor 1 = /dev/console */
    return emit_stat(buf, &kst);
  }

  int rc = vfs_fstat((int)fd, &kst);
  if (rc < 0)
    return rc;

  return emit_stat(buf, &kst);
}

int32_t sys_dup(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4,
                uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  if (is_console_device(fd)) {
    process_t *proc = get_current_process();
    for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
      if (proc->fd_table[i].of_idx == 0 && proc->fd_table[i].flags == 0) {
        proc->fd_table[i] = proc->fd_table[fd];
        return i;
      }
    }
    return -EMFILE;
  }

  return vfs_dup((int)fd);
}

int32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t u3, uint32_t u4,
                 uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (oldfd >= MAX_OPEN_FILES_CONST || newfd >= MAX_OPEN_FILES_CONST)
    return -EBADF;

  if (is_console_device(oldfd)) {
    process_t *proc = get_current_process();

    if (proc->fd_table[newfd].of_idx != 0) {
      sys_close(newfd, 0, 0, 0, 0);
    }

    proc->fd_table[newfd] = proc->fd_table[oldfd];
    return (int32_t)newfd;
  }

  return vfs_dup2((int)oldfd, (int)newfd);
}

int32_t sys_mkdir(uint32_t path, uint32_t mode, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_mkdir(kpath, (mode_t)(mode & 0777));
}

int32_t sys_chdir(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_chdir(kpath);
}

int32_t sys_getcwd(uint32_t buf, uint32_t size, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (!buf || size == 0)
    return -EINVAL;

  process_t *cur = get_current_process();
  const char *cwd = cur->cwd[0] ? cur->cwd : "/";
  size_t len = kstrlen(cwd);

  if (len + 1 > size)
    return -ERANGE;

  int rc = copy_to_user((void *)buf, cwd, len + 1);
  return IS_ERROR(rc) ? rc : (int32_t)buf;
}

int32_t sys_unlink(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_unlink(kpath);
}

int32_t sys_getdents(uint32_t fd, uint32_t buf, uint32_t size, uint32_t u4,
                     uint32_t u5) {
  (void)u4;
  (void)u5;

  if (!buf || size == 0)
    return -EINVAL;

  uint32_t ksz = (size > BUF_SIZE_CONST) ? BUF_SIZE_CONST : size;
  char kbuf[BUF_SIZE_CONST];

  int32_t ret = vfs_readdir_fd((int)fd, kbuf, ksz);
  if (ret <= 0)
    return ret;

  char outbuf[BUF_SIZE_CONST];
  size_t out_pos = 0;
  size_t in_pos = 0;
  size_t user_cap = (size > BUF_SIZE_CONST) ? BUF_SIZE_CONST : size;

  while (in_pos + 10 <= (size_t)ret) {
    uint32_t d_ino = *(uint32_t *)(kbuf + in_pos + 0);
    uint32_t d_off = *(uint32_t *)(kbuf + in_pos + 4);
    uint16_t in_reclen = *(uint16_t *)(kbuf + in_pos + 8);
    if (in_reclen < 11 || in_pos + in_reclen > (size_t)ret)
      break;

    const char *name = kbuf + in_pos + 10;
    size_t name_max = in_reclen - 10;
    size_t name_len = 0;
    while (name_len < name_max && name[name_len] != '\0')
      name_len++;
    if (name_len == name_max)
      break; /* malformed: no NUL */

    size_t needed = 10 + name_len + 1 + 1;
    size_t new_reclen = (needed + 3) & ~3u;

    if (out_pos + new_reclen > user_cap)
      break;

    char *rec = outbuf + out_pos;
    kmemset(rec, 0, new_reclen);
    *(uint32_t *)(rec + 0) = d_ino;
    *(uint32_t *)(rec + 4) = d_off;
    *(uint16_t *)(rec + 8) = (uint16_t)new_reclen;
    kmemcpy(rec + 10, name, name_len);
    rec[10 + name_len] = '\0';
    rec[new_reclen - 1] = vfs_inode_dtype(d_ino);

    out_pos += new_reclen;
    in_pos += in_reclen;
  }

  if (out_pos == 0)
    return 0;

  int rc = copy_to_user((void *)buf, outbuf, out_pos);
  if (IS_ERROR(rc))
    return rc;
  return (int32_t)out_pos;
}

int32_t sys_chmod(uint32_t path, uint32_t mode, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_chmod(kpath, (mode_t)mode);
}

int32_t sys_chown(uint32_t path, uint32_t owner, uint32_t group, uint32_t u4,
                  uint32_t u5) {
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_chown(kpath, (uid_t)owner, (gid_t)group);
}

int32_t sys_rename(uint32_t oldpath, uint32_t newpath, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  char koldpath[PATH_MAX_CONST], knewpath[PATH_MAX_CONST];
  int rc = copy_user_path(oldpath, koldpath);
  if (rc)
    return rc;
  rc = copy_user_path(newpath, knewpath);
  if (rc)
    return rc;

  return vfs_rename(koldpath, knewpath);
}

int32_t sys_rmdir(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4,
                  uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_rmdir(kpath);
}

int32_t sys_truncate(uint32_t path, uint32_t length, uint32_t u3, uint32_t u4,
                     uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  char kpath[PATH_MAX_CONST];
  int rc = copy_user_path(path, kpath);
  if (rc)
    return rc;

  return vfs_truncate(kpath, length);
}

int32_t sys_ftruncate(uint32_t fd, uint32_t length, uint32_t u3, uint32_t u4,
                      uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  return vfs_ftruncate((int)fd, length);
}
