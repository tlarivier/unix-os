#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
};

struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[1];
} __attribute__((packed));

#define VFS_MAX_PATH 4096
#define VFS_MAX_NAME 255

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2

struct vfs_node;
struct file;

typedef struct file_operations {
    ssize_t (*read)(struct file*, char*, size_t, off_t*);
    ssize_t (*write)(struct file*, const char*, size_t, off_t*);
    int (*open)(struct vfs_node*, struct file*);
    int (*release)(struct vfs_node*, struct file*);
    int (*ioctl)(struct file*, unsigned int, unsigned long);
    off_t (*lseek)(struct file*, off_t, int);
    int (*mmap)(struct file*, uint32_t, uint32_t, uint32_t);
    ssize_t (*readdir)(struct file*, void*, size_t);
} file_operations_t;

typedef struct file {
    struct vfs_node* node;
    off_t f_pos;
    uint32_t f_flags;
    uint32_t f_mode;
    const file_operations_t* f_op;
    void* private_data;
} file_t;

typedef struct vfs_node {
    char name[VFS_MAX_NAME];
    uint32_t type;
    uint32_t size;
    uint32_t inode;
    struct vfs_node *parent;
    void *private_data;
    const file_operations_t* default_fops;
} vfs_node_t;

extern const file_operations_t ramfs_file_ops;
extern const file_operations_t ramfs_dir_ops;

int32_t vfs_init(void);
int32_t vfs_open(const char *pathname, int flags, mode_t mode);
void vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
int32_t vfs_mkdir(const char *pathname, mode_t mode);
off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_stat(const char *path, struct stat *st);
int vfs_fstat(int fd, struct stat *st);
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);
ssize_t vfs_readdir_fd(int fd, void *buffer, size_t size);
vfs_node_t *vfs_get_root(void);
vfs_node_t *vfs_resolve_path(const char *path);
void vfs_node_free(vfs_node_t *node);
int vfs_unlink(const char *path);
int vfs_create_from_memory(const char* path, const void* data, uint32_t size);
void vfs_node_incref(int node_idx);
void vfs_node_decref(int node_idx);

#endif
