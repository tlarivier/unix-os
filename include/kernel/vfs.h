#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/syscalls.h>

/* `struct stat` and `struct linux_dirent` are the UAPI structs from
 * <uapi/types.h> and <uapi/syscalls.h>. Kernel writes both layouts
 * directly to user buffers; sys_fs.c does a verbatim copy. */

#define VFS_MAX_PATH 4096
#define VFS_MAX_NAME 255

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR 2

int32_t vfs_init(void);
int32_t vfs_open(const char *pathname, int flags, mode_t mode);
void vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_stat(const char *path, struct stat *st);
int32_t vfs_mkdir(const char *pathname, mode_t mode);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_chdir(const char *path);

static inline int of_idx_decode(uint32_t encoded) {
  if (encoded == 0)
    return -1;
  if (encoded > 64)
    return -1;
  return (int)encoded - 1;
}

typedef enum {
  FD_KIND_INVALID = 0,
  FD_KIND_CONSOLE = 1,
  FD_KIND_PIPE = 2,
  FD_KIND_VFS = 3,
} vfs_fd_kind_t;

vfs_fd_kind_t vfs_fd_kind(int fd, int *pipe_id_out);

#endif
