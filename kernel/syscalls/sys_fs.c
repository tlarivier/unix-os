#include "syscall.h"
#include <kernel/vfs.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/kstring.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/console.h>

#define PATH_MAX PATH_MAX_CONST
#define BUF_SIZE BUF_SIZE_CONST
#define MAX_FDS MAX_OPEN_FILES_CONST

static int resolve_user_path(const char* upath, char* kpath, size_t kpath_size) {
    if (!upath || !kpath) return -EINVAL;
    
    if (upath[0] == '/') {
        int i = 0;
        while (upath[i] && i < (int)kpath_size - 1) {
            kpath[i] = upath[i];
            i++;
        }
        kpath[i] = '\0';
        return 0;
    }
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    int i = 0;
    while (proc->cwd[i] && i < (int)kpath_size - 1) {
        kpath[i] = proc->cwd[i];
        i++;
    }
    
    if (upath[0] == '.' && (upath[1] == '\0' || upath[1] == '/')) {
        const char *rest = (upath[1] == '/') ? &upath[1] : "";
        while (*rest && i < (int)kpath_size - 1) {
            kpath[i++] = *rest++;
        }
        kpath[i] = '\0';
        return 0;
    }
    
    if (i > 0 && kpath[i-1] != '/' && i < (int)kpath_size - 1) {
        kpath[i++] = '/';
    }
    int j = 0;
    while (upath[j] && i < (int)kpath_size - 1) {
        kpath[i++] = upath[j++];
    }
    kpath[i] = '\0';
    return 0;
}

extern int vfs_rename(const char* oldpath, const char* newpath);
extern int vfs_rmdir(const char* path);
extern int vfs_truncate(const char* path, uint32_t length);
extern int vfs_ftruncate(int fd, uint32_t length);

extern void kprintf(const char*, ...);

int32_t sys_open(uint32_t path, uint32_t flags, uint32_t mode, uint32_t u4, uint32_t u5) {
    (void)mode; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char upath[PATH_MAX];
    int rc = copy_str_from_user(upath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    char kpath[PATH_MAX];
    rc = resolve_user_path(upath, kpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return vfs_open(kpath, (int)flags, 0644);
}

static int is_console_device(uint32_t fd) {
    process_t *proc = get_current_process();
    if (!proc || fd >= MAX_OPEN_FILES_CONST) return 0;
    return (proc->fd_table[fd].node_idx == CONSOLE_INODE_MAGIC);
}

static int is_pipe_fd(uint32_t fd, int *pipe_id) {
    process_t *proc = get_current_process();
    if (!proc || fd >= MAX_OPEN_FILES_CONST) return 0;
    
    uint32_t node_idx = proc->fd_table[fd].node_idx;
    if (node_idx >= PIPE_FD_BASE && node_idx != CONSOLE_INODE_MAGIC) {
        if (pipe_id) *pipe_id = node_idx - PIPE_FD_BASE;
        return 1;
    }
    return 0;
}

int32_t sys_close(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    if (is_console_device(fd)) {
        proc->fd_table[fd].node_idx = 0;
        proc->fd_table[fd].flags    = 0;
        proc->fd_table[fd].offset   = 0;
        proc->fd_table[fd].refcount = 0;
        return 0;
    }
    
    int pipe_id;
    if (is_pipe_fd(fd, &pipe_id)) {
        int is_write_end = 0;
        is_write_end = (proc->fd_table[fd].flags & O_WRONLY) ? 1 : 0;
        proc->fd_table[fd].node_idx = 0;
        proc->fd_table[fd].flags = 0;
        return pipe_close_by_id(pipe_id, is_write_end);
    }
    
    vfs_close(fd);
    return 0;
}

uint32_t stdin_flags = 0;

int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t count, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!buf || count == 0) return -EINVAL;
    
    if (is_console_device(fd)) {
        extern char kb_get_char(void);
        extern char kb_try_get_char(void);
        extern int kb_has_char(void);
        
        if (!access_ok((void*)buf, count)) {
            return -EFAULT;
        }
        
        if (stdin_flags & O_NONBLOCK) {
            if (!kb_has_char()) return -EAGAIN;
            char k = kb_try_get_char();
            if (k == 0) return -EAGAIN;
            int rc = copy_to_user((void*)buf, &k, 1);
            return IS_ERROR(rc) ? rc : 1;
        }
        char k = kb_get_char();
        int rc = copy_to_user((void*)buf, &k, 1);
        return IS_ERROR(rc) ? rc : 1;
    }
    
    int pipe_id;
    if (is_pipe_fd(fd, &pipe_id)) {
        return (int32_t)pipe_read_by_id(pipe_id, (void*)buf, count);
    }
    
    char kbuf[BUF_SIZE];
    uint32_t remaining = count;
    int32_t total = 0;
    
    while (remaining > 0) {
        uint32_t chunk = (remaining > BUF_SIZE) ? BUF_SIZE : remaining;
        int32_t r = vfs_read((int)fd, kbuf, chunk);
        if (r <= 0) break;
        
        int rc = copy_to_user((void*)(buf + total), kbuf, (size_t)r);
        if (IS_ERROR(rc)) return (total > 0) ? total : rc;
        
        total += r;
        remaining -= (uint32_t)r;
    }
    return total;
}

int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!buf && count > 0) return -EFAULT;
    if (count == 0) return 0;
    
    int pipe_id;
    if (is_pipe_fd(fd, &pipe_id)) {
        return (int32_t)pipe_write_by_id(pipe_id, (const void*)buf, count);
    }
    
    char kbuf[BUF_SIZE];
    size_t written = 0;
    
    if (is_console_device(fd)) {
        while (written < count) {
            size_t chunk = (count - written > BUF_SIZE) ? BUF_SIZE : (count - written);
            int rc = copy_from_user(kbuf, (const void*)(buf + written), chunk);
            if (rc < 0) return (written > 0) ? (int32_t)written : rc;
            
            for (size_t i = 0; i < chunk; i++) {
                vga_putchar(kbuf[i]);
                serial_putc(kbuf[i]);  
            }
            written += chunk;
        }
        return (int32_t)count;
    }
    
    while (written < count) {
        size_t chunk = (count - written > BUF_SIZE) ? BUF_SIZE : (count - written);
        int rc = copy_from_user(kbuf, (const void*)(buf + written), chunk);
        if (rc < 0) return (written > 0) ? (int32_t)written : rc;
        
        int32_t w = vfs_write((int)fd, kbuf, chunk);
        if (w <= 0) break;
        written += (size_t)w;
    }
    return (int32_t)written;
}

int32_t sys_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    if (whence > 2) return -EINVAL;  
    
    return (int32_t)vfs_lseek((int)fd, (off_t)offset, (int)whence);
}

int32_t sys_stat(uint32_t path, uint32_t buf, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!path || !buf) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    struct stat kst;
    rc = vfs_stat(kpath, &kst);
    if (rc < 0) return rc;
    
    rc = copy_to_user((void*)buf, &kst, sizeof(struct stat));
    return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_fstat(uint32_t fd, uint32_t buf, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!buf) return -EINVAL;
    
    struct stat kst;
    
    if (is_console_device(fd)) {
        kst.st_dev     = 0;
        kst.st_ino     = CONSOLE_INODE_MAGIC;
        kst.st_mode    = S_IFCHR | 0666;  /* Character device, rw-rw-rw- */
        kst.st_nlink   = 1;
        kst.st_uid     = 0;
        kst.st_gid     = 0;
        kst.st_rdev    = 0x0501;  /* Major 5, minor 1 = /dev/console */
        kst.st_size    = 0;
        kst.st_blksize = 0;
        kst.st_blocks  = 0;
        kst.st_atime   = 0;
        kst.st_mtime   = 0;
        kst.st_ctime   = 0;
        int rc = copy_to_user((void*)buf, &kst, sizeof(struct stat));
        return IS_ERROR(rc) ? rc : 0;
    }
    
    int rc = vfs_fstat((int)fd, &kst);
    if (rc < 0) return rc;
    
    rc = copy_to_user((void*)buf, &kst, sizeof(struct stat));
    return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_dup(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    
    if (is_console_device(fd)) {
        process_t *proc = get_current_process();
        if (!proc) return -ESRCH;
        
        for (int i = 0; i < MAX_FDS; i++) {
            if (proc->fd_table[i].node_idx == 0 && proc->fd_table[i].flags == 0) {
                proc->fd_table[i] = proc->fd_table[fd];
                return i;
            }
        }
        return -EMFILE;
    }
    
    return vfs_dup((int)fd);
}

int32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (oldfd >= MAX_FDS || newfd >= MAX_FDS) return -EBADF;
    if (oldfd == newfd) return (int32_t)newfd;
    
    /* Handle console device dup2 */
    if (is_console_device(oldfd)) {
        process_t *proc = get_current_process();
        if (!proc) return -ESRCH;
        
        if (proc->fd_table[newfd].node_idx != 0) {
            sys_close(newfd, 0, 0, 0, 0);
        }
        
        proc->fd_table[newfd] = proc->fd_table[oldfd];
        return (int32_t)newfd;
    }
    
    return vfs_dup2((int)oldfd, (int)newfd);
}

int32_t sys_mkdir(uint32_t path, uint32_t mode, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)mode; (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    if (kpath[0] != '/') return -EINVAL;
    
    return vfs_mkdir(kpath, 0755);
}

int32_t sys_chdir(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    vfs_node_t *node = vfs_resolve_path(kpath);
    if (!node) return -ENOENT;
    if (node->type != VFS_TYPE_DIR) {
        vfs_node_free(node);
        return -ENOTDIR;
    }
    vfs_node_free(node);
    
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    for (int i = 0; i < 255 && kpath[i]; i++) {
        cur->cwd[i] = kpath[i];
        cur->cwd[i+1] = '\0';
    }
    return 0;
}

int32_t sys_getcwd(uint32_t buf, uint32_t size, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!buf || size == 0) return -EINVAL;
    
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    const char* cwd = cur->cwd[0] ? cur->cwd : "/";
    size_t len = kstrlen(cwd);
    
    if (len + 1 > size) return -ERANGE;
    
    int rc = copy_to_user((void*)buf, cwd, len + 1);
    return IS_ERROR(rc) ? rc : (int32_t)buf;
}

int32_t sys_unlink(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return vfs_unlink(kpath);
}

int32_t sys_access(uint32_t path, uint32_t mode, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)mode; (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    struct stat st;
    return vfs_stat(kpath, &st);
}

int32_t sys_getdents(uint32_t fd, uint32_t buf, uint32_t size, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!buf || size == 0) return -EINVAL;
    
    uint32_t ksz = (size > BUF_SIZE) ? BUF_SIZE : size;
    char kbuf[BUF_SIZE];
    
    int32_t ret = vfs_readdir_fd((int)fd, kbuf, ksz);
    if (ret > 0) {
        int rc = copy_to_user((void*)buf, kbuf, (size_t)ret);
        if (IS_ERROR(rc)) return rc;
    }
    return ret;
}

int32_t sys_chmod(uint32_t path, uint32_t mode, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    struct stat st;
    rc = vfs_stat(kpath, &st);
    if (IS_ERROR(rc)) return rc;
    
    /* TODO: Actually store and enforce mode */
    (void)mode;
    return 0;
}

int32_t sys_fchmod(uint32_t fd, uint32_t mode, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    
    /* TODO: Actually implement fchmod */
    (void)mode;
    return 0;
}

int32_t sys_chown(uint32_t path, uint32_t owner, uint32_t group, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    struct stat st;
    rc = vfs_stat(kpath, &st);
    if (IS_ERROR(rc)) return rc;
    
    /* TODO: Actually store owner/group */
    (void)owner; (void)group;
    return 0;
}

int32_t sys_fchown(uint32_t fd, uint32_t owner, uint32_t group, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    
    /* TODO: Actually implement fchown */
    (void)owner; (void)group;
    return 0;
}

int32_t sys_rename(uint32_t oldpath, uint32_t newpath, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!oldpath || !newpath) return -EINVAL;
    
    char koldpath[PATH_MAX], knewpath[PATH_MAX];
    int rc = copy_str_from_user(koldpath, (const char*)oldpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    rc = copy_str_from_user(knewpath, (const char*)newpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return vfs_rename(koldpath, knewpath);
}

int32_t sys_rmdir(uint32_t path, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return vfs_rmdir(kpath);
}

int32_t sys_link(uint32_t oldpath, uint32_t newpath, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!oldpath || !newpath) return -EINVAL;
    
    char koldpath[PATH_MAX], knewpath[PATH_MAX];
    int rc = copy_str_from_user(koldpath, (const char*)oldpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    rc = copy_str_from_user(knewpath, (const char*)newpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    /* Hard links not supported in simple VFS */
    return -ENOSYS;
}

int32_t sys_symlink(uint32_t target, uint32_t linkpath, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!target || !linkpath) return -EINVAL;
    
    char ktarget[PATH_MAX], klinkpath[PATH_MAX];
    int rc = copy_str_from_user(ktarget, (const char*)target, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    rc = copy_str_from_user(klinkpath, (const char*)linkpath, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return -ENOSYS;
}

int32_t sys_readlink(uint32_t path, uint32_t buf, uint32_t bufsiz, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!path || !buf || bufsiz == 0) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    /* Symlinks not supported */
    return -EINVAL;
}

int32_t sys_umask(uint32_t mask, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return 022;  /* Default umask */
    
    uint32_t old_umask = proc->umask;
    proc->umask = mask & 0777;
    return old_umask;
}

int32_t sys_truncate(uint32_t path, uint32_t length, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!path) return -EINVAL;
    
    char kpath[PATH_MAX];
    int rc = copy_str_from_user(kpath, (const char*)path, PATH_MAX);
    if (IS_ERROR(rc)) return rc;
    
    return vfs_truncate(kpath, length);
}

int32_t sys_ftruncate(uint32_t fd, uint32_t length, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    return vfs_ftruncate((int)fd, length);
}

#define LOCK_SH 1  
#define LOCK_EX 2  
#define LOCK_NB 4  
#define LOCK_UN 8  

int32_t sys_flock(uint32_t fd, uint32_t operation, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (fd >= MAX_FDS) return -EBADF;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    /* Basic flock implementation - for now just validate and succeed */
    /* Real implementation would need per-inode lock tracking */
    int op = operation & ~LOCK_NB;
    
    switch (op) {
        case LOCK_SH:
        case LOCK_EX:
        case LOCK_UN:
            /* Accept but don't actually lock (single-user system) */
            return 0;
        default:
            return -EINVAL;
    }
}
