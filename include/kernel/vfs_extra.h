#ifndef KERNEL_VFS_EXTRA_H
#define KERNEL_VFS_EXTRA_H

#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

ssize_t vfs_pread(int fd, void *buf, size_t count, uint32_t offset);
const char *vfs_get_path_by_fd(int fd);
void vfs_open_file_incref(int raw_of_idx);
uint8_t vfs_inode_dtype(uint32_t inode_idx);
struct stat;
int vfs_fstat(int fd, struct stat *st);
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);
ssize_t vfs_readdir_fd(int fd, void *buffer, size_t size);
int vfs_rmdir(const char *path);
int vfs_truncate(const char *path, uint32_t length);
int vfs_ftruncate(int fd, uint32_t length);
int vfs_chmod(const char *path, mode_t mode);
int vfs_chown(const char *path, uid_t uid, gid_t gid);
int vfs_mount_ext2_at(const char *target);

#endif /* KERNEL_VFS_EXTRA_H */
