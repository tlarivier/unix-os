/**
 * vfs.c - Virtual File System layer
 *
 * VFS API layer that delegates to ramfs.c for actual filesystem operations.
 * Manages open file table and provides POSIX-like interface.
 */

#include <kernel/vfs.h>
#include <kernel/ramfs.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/process.h>
#include <kernel/constants.h>
#include <kernel/kprintf.h>
#include <../uapi/syscalls.h>

#define VFS_MAX_OPEN_FILES  64

/* Open file instance - tracks per-open state */
typedef struct vfs_open_file {
    uint32_t inode;
    uint32_t pos;
    uint32_t flags;
    uint32_t refcount;
} vfs_open_file_t;

/* Global VFS state */
static vfs_open_file_t open_files[VFS_MAX_OPEN_FILES];
static spinlock_t vfs_lock = SPINLOCK_INIT("vfs");
static int vfs_initialized = 0;

/* Kernel-mode fd table for use when no process context exists */
typedef struct {
    uint32_t node_idx;
    uint32_t flags;
    uint32_t offset;
    uint32_t refcount;
} kernel_fd_entry_t;
static kernel_fd_entry_t kernel_fd_table[VFS_MAX_OPEN_FILES];

/* Forward declarations */
static int vfs_alloc_open_file(void);
static void vfs_free_open_file(int idx);
static uint32_t vfs_path_to_inode(const char* path);

/* Path resolution - delegates to ramfs */
static uint32_t vfs_path_to_inode(const char* path) {
    return ramfs_path_to_inode(path);
}

/*
 * Allocate an open file descriptor slot
 */
static int vfs_alloc_open_file(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (open_files[i].refcount == 0) {
            memset(&open_files[i], 0, sizeof(vfs_open_file_t));
            open_files[i].refcount = 1;
            return i;
        }
    }
    return -EMFILE;
}

/*
 * Free an open file descriptor slot
 */
static void vfs_free_open_file(int idx) {
    if (idx >= 0 && idx < VFS_MAX_OPEN_FILES) {
        open_files[idx].refcount = 0;
    }
}

/* ========================================================================
 * Public VFS API
 * ======================================================================== */

/*
 * Initialize VFS - delegates to ramfs
 */
int32_t vfs_init(void) {
    /* Clear open files */
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].refcount = 0;
    }
    
    /* Initialize ramfs */
    ramfs_init();
    vfs_initialized = 1;
    
    kprintf("VFS initialized (ramfs, %d inodes)\n", RAMFS_MAX_INODES);
    return 0;
}

/*
 * Get root node for external access
 */
vfs_node_t* vfs_get_root(void) {
    static vfs_node_t root_node;
    ramfs_inode_t* root = ramfs_get_inode(0);
    if (!root) return NULL;
    
    kstrncpy(root_node.name, root->name, VFS_MAX_NAME);
    root_node.type = (root->type == RAMFS_INODE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    root_node.size = root->size;
    root_node.inode = 0;
    root_node.parent = NULL;
    
    return &root_node;
}

/*
 * Resolve path to vfs_node
 */
vfs_node_t* vfs_resolve_path(const char* path) {
    if (!vfs_initialized || !path) return NULL;
    
    spin_lock(&vfs_lock);
    uint32_t idx = vfs_path_to_inode(path);
    spin_unlock(&vfs_lock);
    
    if (idx == 0 && kstrcmp(path, "/") != 0) return NULL;
    
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    ramfs_inode_t* inode = ramfs_get_inode(idx);
    if (!inode) { kfree(node); return NULL; }
    
    kstrncpy(node->name, inode->name, VFS_MAX_NAME);
    node->type = (inode->type == RAMFS_INODE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    node->size = inode->size;
    node->inode = idx;
    node->parent = NULL;
    
    return node;
}

/*
 * Free a vfs_node allocated by vfs_resolve_path
 * VFS-001: Added to prevent memory leaks from dynamic allocation
 */
void vfs_node_free(vfs_node_t* node) {
    if (node) {
        kfree(node);
    }
}

/*
 * Create directory
 */
/* Helper: extract parent path and basename from full path */
static int split_path(const char* path, char* parent, char* name, size_t size) {
    kstrncpy(parent, path, size);
    int len = kstrlen(parent);
    while (len > 1 && parent[len - 1] == '/') parent[--len] = '\0';
    
    int last_slash = 0;
    for (int i = len - 1; i >= 0; i--) {
        if (parent[i] == '/') { last_slash = i; break; }
    }
    kstrncpy(name, parent + last_slash + 1, size);
    parent[last_slash == 0 ? 1 : last_slash] = '\0';
    return 0;
}

int32_t vfs_mkdir(const char* pathname, mode_t mode) {
    if (!vfs_initialized || !pathname || pathname[0] != '/') return -EINVAL;
    
    char parent_path[VFS_MAX_PATH], dir_name[VFS_MAX_NAME + 1];
    split_path(pathname, parent_path, dir_name, sizeof(parent_path));
    
    spin_lock(&vfs_lock);
    uint32_t parent_idx = vfs_path_to_inode(parent_path);
    if (parent_idx == 0 && kstrcmp(parent_path, "/") != 0) {
        spin_unlock(&vfs_lock); return -ENOENT;
    }
    
    int rc = ramfs_create_dir(parent_idx, dir_name, mode, NULL);
    spin_unlock(&vfs_lock);
    return rc;
}

/*
 * Open file - returns file descriptor
 */
int32_t vfs_open(const char* pathname, int flags, mode_t mode) {
    if (!vfs_initialized || !pathname) return -EINVAL;
    
    spin_lock(&vfs_lock);
    
    uint32_t inode_idx = vfs_path_to_inode(pathname);
    
    /* Handle O_CREAT */
    if (inode_idx == 0 && (flags & O_CREAT)) {
        char parent_path[VFS_MAX_PATH], file_name[VFS_MAX_NAME + 1];
        split_path(pathname, parent_path, file_name, sizeof(parent_path));
        
        uint32_t parent_idx = vfs_path_to_inode(parent_path);
        if (parent_idx == 0 && kstrcmp(parent_path, "/") != 0) {
            spin_unlock(&vfs_lock); return -ENOENT;
        }
        
        int rc = ramfs_create_file(parent_idx, file_name, mode, &inode_idx);
        if (rc < 0) { spin_unlock(&vfs_lock); return rc; }
    } else if (inode_idx == 0 && kstrcmp(pathname, "/") != 0) {
        spin_unlock(&vfs_lock); return -ENOENT;
    }
    
    /* Handle O_TRUNC */
    if (flags & O_TRUNC) ramfs_truncate_inode(inode_idx, 0);
    
    /* Allocate open file slot */
    int of_idx = vfs_alloc_open_file();
    if (of_idx < 0) { spin_unlock(&vfs_lock); return -EMFILE; }
    
    ramfs_inode_t* inode = ramfs_get_inode(inode_idx);
    open_files[of_idx].inode = inode_idx;
    open_files[of_idx].pos = (flags & O_APPEND) ? inode->size : 0;
    open_files[of_idx].flags = flags;
    
    /* Find free fd in process fd_table or kernel fd_table */
    process_t* proc = get_current_process();
    int fd = -1;
    
    if (proc) {
        for (int i = 3; i < MAX_OPEN_FILES_CONST; i++) {
            if (proc->fd_table[i].node_idx == 0 && proc->fd_table[i].flags == 0) {
                fd = i;
                proc->fd_table[i].node_idx = of_idx + 1;  /* +1 to distinguish from empty */
                proc->fd_table[i].flags = flags;
                proc->fd_table[i].offset = 0;
                proc->fd_table[i].refcount = 1;
                break;
            }
        }
    } else {
        /* Kernel mode - use kernel fd table */
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (kernel_fd_table[i].node_idx == 0) {
                fd = i;
                kernel_fd_table[i].node_idx = of_idx + 1;
                kernel_fd_table[i].flags = flags;
                kernel_fd_table[i].offset = 0;
                kernel_fd_table[i].refcount = 1;
                break;
            }
        }
    }
    
    if (fd < 0) {
        vfs_free_open_file(of_idx);
        spin_unlock(&vfs_lock);
        return -EMFILE;
    }
    
    ramfs_get_inode(inode_idx)->nlink++;
    
#ifdef DEBUG_VFS
    kprintf("vfs_open: path=%s fd=%d of_idx=%d\n", pathname, fd, of_idx);
#endif
    spin_unlock(&vfs_lock);
    return fd;
}

/*
 * Close file descriptor
 */
void vfs_close(int fd) {
    if (!vfs_initialized || fd < 0) return;
    
    process_t* proc = get_current_process();
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx;
    if (proc) {
        if (fd >= MAX_OPEN_FILES_CONST) { spin_unlock(&vfs_lock); return; }
        of_idx = proc->fd_table[fd].node_idx;
    } else {
        if (fd >= VFS_MAX_OPEN_FILES) { spin_unlock(&vfs_lock); return; }
        of_idx = kernel_fd_table[fd].node_idx;
    }
    
    if (of_idx == 0) {
        spin_unlock(&vfs_lock);
        return;
    }
    of_idx--;  /* Convert back from +1 encoding */
    
    if (of_idx < VFS_MAX_OPEN_FILES && open_files[of_idx].refcount > 0) {
        open_files[of_idx].refcount--;
        if (open_files[of_idx].refcount == 0) {
            uint32_t inode_idx = open_files[of_idx].inode;
            if (inode_idx < RAMFS_MAX_INODES && ramfs_get_inode(inode_idx)->nlink > 0) {
                ramfs_get_inode(inode_idx)->nlink--;
            }
            vfs_free_open_file(of_idx);
        }
    }
    
    if (proc) {
        proc->fd_table[fd].node_idx = 0;
        proc->fd_table[fd].flags = 0;
        proc->fd_table[fd].offset = 0;
        proc->fd_table[fd].refcount = 0;
    } else {
        kernel_fd_table[fd].node_idx = 0;
        kernel_fd_table[fd].flags = 0;
        kernel_fd_table[fd].offset = 0;
        kernel_fd_table[fd].refcount = 0;
    }
    
    spin_unlock(&vfs_lock);
}

/*
 * Read from file
 */
ssize_t vfs_read(int fd, void* buf, size_t count) {
    if (!vfs_initialized || !buf || fd < 0 || fd >= MAX_OPEN_FILES_CONST) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) { spin_unlock(&vfs_lock); return -EBADF; }
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES || open_files[of_idx].refcount == 0) {
        spin_unlock(&vfs_lock); return -EBADF;
    }
    
    vfs_open_file_t* of = &open_files[of_idx];
    ssize_t result = ramfs_read_data(of->inode, buf, count, of->pos);
    if (result > 0) of->pos += result;
    
    spin_unlock(&vfs_lock);
    return result;
}

/*
 * Write to file
 */
ssize_t vfs_write(int fd, const void* buf, size_t count) {
    if (!vfs_initialized || !buf || fd < 0) return -EINVAL;
    if (count == 0) return 0;
    
    process_t* proc = get_current_process();
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx;
    if (proc) {
        if (fd >= MAX_OPEN_FILES_CONST) { spin_unlock(&vfs_lock); return -EBADF; }
        of_idx = proc->fd_table[fd].node_idx;
    } else {
        if (fd >= VFS_MAX_OPEN_FILES) { spin_unlock(&vfs_lock); return -EBADF; }
        of_idx = kernel_fd_table[fd].node_idx;
    }
    if (of_idx == 0) { spin_unlock(&vfs_lock); return -EBADF; }
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES || open_files[of_idx].refcount == 0) {
        spin_unlock(&vfs_lock); return -EBADF;
    }
    
    vfs_open_file_t* of = &open_files[of_idx];
    
    /* Handle O_APPEND */
    if (of->flags & O_APPEND) {
        ramfs_inode_t* inode = ramfs_get_inode(of->inode);
        if (inode) of->pos = inode->size;
    }
    
    ssize_t result = ramfs_write_data(of->inode, buf, count, of->pos);
    if (result > 0) {
        of->pos += result;
    }
    
    spin_unlock(&vfs_lock);
    return result;
}

/*
 * Seek within file
 */
off_t vfs_lseek(int fd, off_t offset, int whence) {
    if (!vfs_initialized || fd < 0 || fd >= MAX_OPEN_FILES_CONST) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc) {
        kprintf("vfs_lseek: no process for fd=%d\n", fd);
        return -ESRCH;
    }
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) {
        spin_unlock(&vfs_lock);
        return -EBADF;
    }
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES || open_files[of_idx].refcount == 0) {
        spin_unlock(&vfs_lock);
        return -EBADF;
    }
    
    vfs_open_file_t* of = &open_files[of_idx];
    ramfs_inode_t* inode = ramfs_get_inode(of->inode);
    
    int32_t new_pos;
    switch (whence) {
        case 0:  /* SEEK_SET */
            new_pos = offset;
            break;
        case 1:  /* SEEK_CUR */
            new_pos = (int32_t)of->pos + offset;
            break;
        case 2:  /* SEEK_END */
            new_pos = (int32_t)inode->size + offset;
            break;
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

/*
 * Get file status by path
 */
int vfs_stat(const char* path, struct stat* st) {
    if (!vfs_initialized || !path || !st) return -EINVAL;
    
    spin_lock(&vfs_lock);
    uint32_t inode_idx = vfs_path_to_inode(path);
    if (inode_idx == 0 && kstrcmp(path, "/") != 0) {
        spin_unlock(&vfs_lock); return -ENOENT;
    }
    int result = ramfs_fill_stat(inode_idx, st);
    spin_unlock(&vfs_lock);
    return result < 0 ? -ENOENT : 0;
}

/*
 * Get file status by fd
 */
int vfs_fstat(int fd, struct stat* st) {
    if (!vfs_initialized || !st || fd < 0 || fd >= MAX_OPEN_FILES_CONST) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) { spin_unlock(&vfs_lock); return -EBADF; }
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES || open_files[of_idx].refcount == 0) {
        spin_unlock(&vfs_lock); return -EBADF;
    }
    
    int result = ramfs_fill_stat(open_files[of_idx].inode, st);
    spin_unlock(&vfs_lock);
    return result < 0 ? -EBADF : 0;
}

/*
 * Duplicate file descriptor
 */
int vfs_dup(int oldfd) {
    if (!vfs_initialized || oldfd < 0 || oldfd >= MAX_OPEN_FILES_CONST) return -EBADF;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[oldfd].node_idx;
    if (of_idx == 0) {
        spin_unlock(&vfs_lock);
        return -EBADF;
    }
    
    /* Find free fd */
    int newfd = -1;
    for (int i = 3; i < MAX_OPEN_FILES_CONST; i++) {
        if (proc->fd_table[i].node_idx == 0) {
            newfd = i;
            break;
        }
    }
    
    if (newfd < 0) {
        spin_unlock(&vfs_lock);
        return -EMFILE;
    }
    
    /* Copy fd entry and increment refcount */
    proc->fd_table[newfd] = proc->fd_table[oldfd];
    open_files[of_idx - 1].refcount++;
    
    spin_unlock(&vfs_lock);
    return newfd;
}

/*
 * Duplicate file descriptor to specific fd
 */
int vfs_dup2(int oldfd, int newfd) {
    if (!vfs_initialized || oldfd < 0 || newfd < 0) return -EBADF;
    if (oldfd >= MAX_OPEN_FILES_CONST || newfd >= MAX_OPEN_FILES_CONST) return -EBADF;
    if (oldfd == newfd) return newfd;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[oldfd].node_idx;
    if (of_idx == 0) {
        spin_unlock(&vfs_lock);
        return -EBADF;
    }
    
    /* Close newfd if open */
    if (proc->fd_table[newfd].node_idx != 0) {
        spin_unlock(&vfs_lock);
        vfs_close(newfd);
        spin_lock(&vfs_lock);
    }
    
    /* Copy fd entry and increment refcount */
    proc->fd_table[newfd] = proc->fd_table[oldfd];
    open_files[of_idx - 1].refcount++;
    
    spin_unlock(&vfs_lock);
    return newfd;
}

/*
 * Read directory entries
 */
ssize_t vfs_readdir_fd(int fd, void* buffer, size_t size) {
    if (!vfs_initialized || !buffer || fd < 0) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    /* fd_table stores of_idx+1, so 0 means empty */
    if (fd >= MAX_OPEN_FILES_CONST) {
        spin_unlock(&vfs_lock);
        kprintf("readdir: fd %d >= MAX\n", fd);
        return -EBADF;
    }
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) {
        spin_unlock(&vfs_lock);
        kprintf("readdir: fd %d node_idx=0\n", fd);
        return -EBADF;
    }
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES) {
        spin_unlock(&vfs_lock);
        kprintf("readdir: of_idx %d >= MAX\n", of_idx);
        return -EBADF;
    }
    
    uint32_t inode_idx = open_files[of_idx].inode;
    ramfs_inode_t* dir = ramfs_get_inode(inode_idx);
    
    if (dir->type != RAMFS_INODE_DIR) {
        spin_unlock(&vfs_lock);
        kprintf("readdir: inode %d type=%d not DIR\n", inode_idx, dir->type);
        return -ENOTDIR;
    }
    
    uint8_t* out = (uint8_t*)buffer;
    size_t written = 0;
    uint32_t pos = open_files[of_idx].pos;
    
    /* Iterate children starting from pos */
    for (uint32_t i = pos; i < dir->child_count; i++) {
        uint32_t child = dir->children[i];
        ramfs_inode_t* child_inode = ramfs_get_inode(child);
        
        size_t name_len = kstrlen(child_inode->name);
        /* Use offsetof to get exact position of d_name, then add name + null */
        size_t reclen = __builtin_offsetof(struct linux_dirent, d_name) + name_len + 1;
        reclen = (reclen + 3) & ~3;  /* Align to 4 bytes */
        
        if (written + reclen > size) break;
        
        struct linux_dirent* ent = (struct linux_dirent*)(out + written);
        ent->d_ino = child;
        ent->d_off = i + 1;
        ent->d_reclen = (uint16_t)reclen;
        memcpy(ent->d_name, child_inode->name, name_len + 1);
        
        written += reclen;
        open_files[of_idx].pos = i + 1;
    }
    
    spin_unlock(&vfs_lock);
    return (ssize_t)written;
}

/*
 * Unlink (delete) file
 */
int vfs_unlink(const char* path) {
    if (!vfs_initialized || !path) return -EINVAL;
    
    spin_lock(&vfs_lock);
    uint32_t inode_idx = vfs_path_to_inode(path);
    if (inode_idx == 0) { spin_unlock(&vfs_lock); return -ENOENT; }
    
    ramfs_inode_t* inode = ramfs_get_inode(inode_idx);
    if (inode->type == RAMFS_INODE_DIR) { spin_unlock(&vfs_lock); return -EISDIR; }
    
    ramfs_remove_child(inode->parent, inode_idx);
    if (--inode->nlink == 0) ramfs_free_inode(inode_idx);
    
    spin_unlock(&vfs_lock);
    return 0;
}

/*
 * Create file from memory buffer - used for embedding binaries
 */
int vfs_create_from_memory(const char* path, const void* data, uint32_t size) {
    if (!vfs_initialized || !path || !data || size == 0) return -EINVAL;
    
    int fd = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (fd < 0) return fd;
    
    ssize_t written = vfs_write(fd, data, size);
    vfs_close(fd);
    return (written == (ssize_t)size) ? 0 : -EIO;
}

/*
 * Increment inode reference count
 */
void vfs_node_incref(int node_idx) {
    if (!vfs_initialized || node_idx < 0 || (uint32_t)node_idx >= VFS_MAX_OPEN_FILES) return;
    
    spin_lock(&vfs_lock);
    if (open_files[node_idx].refcount > 0) {
        open_files[node_idx].refcount++;
    }
    spin_unlock(&vfs_lock);
}

/*
 * Decrement inode reference count
 */
void vfs_node_decref(int node_idx) {
    if (!vfs_initialized || node_idx < 0 || (uint32_t)node_idx >= VFS_MAX_OPEN_FILES) return;
    
    spin_lock(&vfs_lock);
    if (open_files[node_idx].refcount > 0) {
        open_files[node_idx].refcount--;
    }
    spin_unlock(&vfs_lock);
}

/*
 * Rename file or directory
 */
int vfs_rename(const char* oldpath, const char* newpath) {
    if (!vfs_initialized || !oldpath || !newpath) return -EINVAL;
    
    char parent_path[VFS_MAX_PATH], new_name[VFS_MAX_NAME + 1];
    split_path(newpath, parent_path, new_name, sizeof(parent_path));
    
    spin_lock(&vfs_lock);
    
    uint32_t old_idx = vfs_path_to_inode(oldpath);
    if (old_idx == 0) { spin_unlock(&vfs_lock); return -ENOENT; }
    
    uint32_t new_parent = vfs_path_to_inode(parent_path);
    if (new_parent == 0 && kstrcmp(parent_path, "/") != 0) {
        spin_unlock(&vfs_lock); return -ENOENT;
    }
    
    ramfs_inode_t* node = ramfs_get_inode(old_idx);
    ramfs_remove_child(node->parent, old_idx);
    kstrncpy(node->name, new_name, RAMFS_MAX_NAME);
    ramfs_add_child(new_parent, old_idx);
    
    spin_unlock(&vfs_lock);
    return 0;
}

/*
 * Remove directory
 */
int vfs_rmdir(const char* path) {
    if (!vfs_initialized || !path) return -EINVAL;
    
    spin_lock(&vfs_lock);
    uint32_t inode_idx = vfs_path_to_inode(path);
    if (inode_idx == 0) { spin_unlock(&vfs_lock); return -ENOENT; }
    
    ramfs_inode_t* dir = ramfs_get_inode(inode_idx);
    if (dir->type != RAMFS_INODE_DIR) { spin_unlock(&vfs_lock); return -ENOTDIR; }
    if (dir->child_count > 0) { spin_unlock(&vfs_lock); return -ENOTEMPTY; }
    
    ramfs_remove_child(dir->parent, inode_idx);
    ramfs_get_inode(dir->parent)->nlink--;
    ramfs_free_inode(inode_idx);
    
    spin_unlock(&vfs_lock);
    return 0;
}

/*
 * Truncate file by path
 */
int vfs_truncate(const char* path, uint32_t length) {
    if (!vfs_initialized || !path) return -EINVAL;
    
    spin_lock(&vfs_lock);
    uint32_t inode_idx = vfs_path_to_inode(path);
    if (inode_idx == 0) { spin_unlock(&vfs_lock); return -ENOENT; }
    
    int result = ramfs_truncate_inode(inode_idx, length);
    spin_unlock(&vfs_lock);
    return result;
}

/*
 * Truncate file by fd
 */
int vfs_ftruncate(int fd, uint32_t length) {
    if (!vfs_initialized || fd < 0 || fd >= MAX_OPEN_FILES_CONST) return -EBADF;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&vfs_lock);
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) { spin_unlock(&vfs_lock); return -EBADF; }
    of_idx--;
    
    uint32_t inode_idx = open_files[of_idx].inode;
    int result = ramfs_truncate_inode(inode_idx, length);
    
    spin_unlock(&vfs_lock);
    return result;
}

/*
 * Get path from fd - for sys_mem.c fb device check
 */
const char* vfs_get_path_by_fd(int fd) {
    if (!vfs_initialized || fd < 0 || fd >= MAX_OPEN_FILES_CONST) return NULL;
    
    process_t* proc = get_current_process();
    if (!proc) return NULL;
    
    uint32_t of_idx = proc->fd_table[fd].node_idx;
    if (of_idx == 0) return NULL;
    of_idx--;
    
    if (of_idx >= VFS_MAX_OPEN_FILES) return NULL;
    
    uint32_t inode_idx = open_files[of_idx].inode;
    if (inode_idx >= RAMFS_MAX_INODES) return NULL;
    
    /* Return inode name - simplified, doesn't return full path */
    return ramfs_get_inode(inode_idx)->name;
}

/* Ramfs file operations - for compatibility with file_operations interface */
const file_operations_t ramfs_file_ops = {
    .read = NULL,   /* VFS handles read directly */
    .write = NULL,  /* VFS handles write directly */
    .open = NULL,
    .release = NULL,
    .ioctl = NULL,
    .lseek = NULL,
    .mmap = NULL,
    .readdir = NULL
};

const file_operations_t ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .release = NULL,
    .ioctl = NULL,
    .lseek = NULL,
    .mmap = NULL,
    .readdir = NULL
};

/*
 * Accessor functions for persistence layer
 */
ramfs_inode_t* vfs_get_inode_table(void) {
    return (ramfs_inode_t*)ramfs_get_inode_table();
}

uint32_t vfs_get_inode_count(void) {
    return ramfs_get_inode_count();
}
