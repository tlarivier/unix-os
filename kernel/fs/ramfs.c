#include <kernel/ramfs.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/vfs.h>
#include <kernel/kprintf.h>

#ifndef S_IFDIR
#define S_IFDIR  0040000
#define S_IFREG  0100000
#endif

static ramfs_inode_t inode_table[RAMFS_MAX_INODES];
static spinlock_t ramfs_lock = SPINLOCK_INIT("ramfs");
static int ramfs_initialized = 0;

uint32_t ramfs_alloc_inode(void) {
    for (uint32_t i = 1; i < RAMFS_MAX_INODES; i++) {
        if (inode_table[i].type == RAMFS_INODE_FREE) {
            memset(&inode_table[i], 0, sizeof(ramfs_inode_t));
            inode_table[i].nlink = 1;
            return i;
        }
    }
    return 0;
}

void ramfs_free_inode(uint32_t idx) {
    if (idx == 0 || idx >= RAMFS_MAX_INODES) return;
    
    ramfs_inode_t* node = &inode_table[idx];
    if (node->data) {
        kfree(node->data);
        node->data = NULL;
    }
    memset(node, 0, sizeof(ramfs_inode_t));
}

ramfs_inode_t* ramfs_get_inode(uint32_t idx) {
    if (idx >= RAMFS_MAX_INODES) return NULL;
    return &inode_table[idx];
}

uint32_t ramfs_lookup_child(uint32_t parent, const char* name) {
    if (parent >= RAMFS_MAX_INODES) return 0;
    ramfs_inode_t* pnode = &inode_table[parent];
    
    if (pnode->type != RAMFS_INODE_DIR) return 0;
    
    for (uint32_t i = 0; i < pnode->child_count; i++) {
        uint32_t child = pnode->children[i];
        if (child < RAMFS_MAX_INODES && 
            kstrcmp(inode_table[child].name, name) == 0) {
            return child;
        }
    }
    return 0;
}

int ramfs_add_child(uint32_t parent, uint32_t child) {
    if (parent >= RAMFS_MAX_INODES || child >= RAMFS_MAX_INODES) return -EINVAL;
    
    ramfs_inode_t* pnode = &inode_table[parent];
    if (pnode->type != RAMFS_INODE_DIR) return -ENOTDIR;
    if (pnode->child_count >= RAMFS_MAX_CHILDREN) return -ENOSPC;
    
    pnode->children[pnode->child_count++] = child;
    inode_table[child].parent = parent;
    return 0;
}

int ramfs_remove_child(uint32_t parent, uint32_t child) {
    if (parent >= RAMFS_MAX_INODES) return -EINVAL;
    
    ramfs_inode_t* pnode = &inode_table[parent];
    if (pnode->type != RAMFS_INODE_DIR) return -ENOTDIR;
    
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

uint32_t ramfs_path_to_inode(const char* path) {
    if (!path || !ramfs_initialized) return 0;
    
    if (path[0] == '/' && path[1] == '\0') return 0;
    
    uint32_t current = 0;  /* Start at root */
    char component[64];
    int ci = 0;
    
    const char* p = path;
    if (*p == '/') p++;
    
    while (*p) {
        if (*p == '/') {
            if (ci > 0) {
                component[ci] = '\0';
                current = ramfs_lookup_child(current, component);
                if (current == 0 && !(ci == 1 && component[0] == '/')) {
                    current = ramfs_lookup_child(0, component);
                    if (current == 0) return 0;
                }
                ci = 0;
            }
            p++;
        } else {
            if (ci < 63) component[ci++] = *p;
            p++;
        }
    }
    
    if (ci > 0) {
        component[ci] = '\0';
        uint32_t found = ramfs_lookup_child(current, component);
        if (found == 0 && current == 0) {
            found = ramfs_lookup_child(0, component);
        }
        return found;
    }
    
    return current;
}

ssize_t ramfs_read_data(uint32_t inode_idx, void* buf, size_t count, uint32_t offset) {
    if (inode_idx >= RAMFS_MAX_INODES || !buf) return -EINVAL;
    
    ramfs_inode_t* inode = &inode_table[inode_idx];
    if (inode->type != RAMFS_INODE_FILE) return -EISDIR;
    
    if (offset >= inode->size) return 0;
    
    uint32_t available = inode->size - offset;
    uint32_t to_read = (count < available) ? count : available;
    
    if (to_read == 0) return 0;
    
    if (inode->data) {
        memcpy(buf, inode->data + offset, to_read);
    }
    
    return (ssize_t)to_read;
}

ssize_t ramfs_write_data(uint32_t inode_idx, const void* buf, size_t count, uint32_t offset) {
    if (inode_idx >= RAMFS_MAX_INODES || !buf) return -EINVAL;
    
    ramfs_inode_t* inode = &inode_table[inode_idx];
    if (inode->type == RAMFS_INODE_DIR) return -EISDIR;
    
    uint32_t end_pos = offset + count;
    if (end_pos > RAMFS_MAX_FILE_SIZE) return -EFBIG;
    
    if (end_pos > inode->data_capacity) {
        uint32_t new_cap = (end_pos + RAMFS_BLOCK_SIZE - 1) & ~(RAMFS_BLOCK_SIZE - 1);
        if (new_cap > RAMFS_MAX_FILE_SIZE) new_cap = RAMFS_MAX_FILE_SIZE;
        
        uint8_t* new_data = kmalloc(new_cap);
        if (!new_data) {
            return -ENOSPC;
        }
        
        memset(new_data, 0, new_cap);
        if (inode->data && inode->size > 0) {
            memcpy(new_data, inode->data, inode->size);
            kfree(inode->data);
        }
        inode->data = new_data;
        inode->data_capacity = new_cap;
    }
    
    memcpy(inode->data + offset, buf, count);
    if (end_pos > inode->size) inode->size = end_pos;
    return (ssize_t)count;
}

int ramfs_truncate_inode(uint32_t inode_idx, uint32_t length) {
    if (inode_idx >= RAMFS_MAX_INODES) return -EINVAL;
    
    ramfs_inode_t* inode = &inode_table[inode_idx];
    if (inode->type != RAMFS_INODE_FILE) return -EISDIR;
    if (length > RAMFS_MAX_FILE_SIZE) return -EFBIG;
    
    if (length > inode->data_capacity) {
        uint32_t new_cap = (length + RAMFS_BLOCK_SIZE - 1) & ~(RAMFS_BLOCK_SIZE - 1);
        uint8_t* new_data = kmalloc(new_cap);
        if (!new_data) return -ENOSPC;
        
        memset(new_data, 0, new_cap);
        if (inode->data && inode->size > 0) {
            uint32_t copy_size = (inode->size < length) ? inode->size : length;
            memcpy(new_data, inode->data, copy_size);
            kfree(inode->data);
        }
        inode->data = new_data;
        inode->data_capacity = new_cap;
    }
    
    inode->size = length;
    return 0;
}

int ramfs_init(void) {
    memset(inode_table, 0, sizeof(inode_table));
    
    inode_table[0].type = RAMFS_INODE_DIR;
    inode_table[0].mode = 0755;
    inode_table[0].nlink = 2;
    kstrcpy(inode_table[0].name, "/");
    
    ramfs_initialized = 1;
    return 0;
}

int ramfs_is_initialized(void) {
    return ramfs_initialized;
}

spinlock_t* ramfs_get_lock(void) {
    return &ramfs_lock;
}

uint32_t ramfs_get_max_inodes(void) {
    return RAMFS_MAX_INODES;
}

uint32_t ramfs_get_inode_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < RAMFS_MAX_INODES; i++) {
        if (inode_table[i].type != RAMFS_INODE_FREE) count++;
    }
    return count;
}

int ramfs_create_dir(uint32_t parent_idx, const char* name, uint32_t mode, uint32_t* out_idx) {
    ramfs_inode_t* parent = ramfs_get_inode(parent_idx);
    if (!parent || parent->type != RAMFS_INODE_DIR) return -ENOTDIR;
    if (ramfs_lookup_child(parent_idx, name) != 0) return -EEXIST;
    
    uint32_t new_idx = ramfs_alloc_inode();
    if (new_idx == 0) return -ENOSPC;
    
    ramfs_inode_t* dir = &inode_table[new_idx];
    dir->type = RAMFS_INODE_DIR;
    dir->mode = mode & 0777;
    dir->nlink = 2;
    kstrncpy(dir->name, name, RAMFS_MAX_NAME);
    
    int rc = ramfs_add_child(parent_idx, new_idx);
    if (rc < 0) { ramfs_free_inode(new_idx); return rc; }
    
    parent->nlink++;
    if (out_idx) *out_idx = new_idx;
    return 0;
}

int ramfs_create_file(uint32_t parent_idx, const char* name, uint32_t mode, uint32_t* out_idx) {
    ramfs_inode_t* parent = ramfs_get_inode(parent_idx);
    if (!parent || parent->type != RAMFS_INODE_DIR) return -ENOTDIR;
    
    uint32_t new_idx = ramfs_alloc_inode();
    if (new_idx == 0) return -ENOSPC;
    
    ramfs_inode_t* file = &inode_table[new_idx];
    file->type = RAMFS_INODE_FILE;
    file->mode = mode & 0777;
    file->nlink = 1;
    kstrncpy(file->name, name, RAMFS_MAX_NAME);
    
    int rc = ramfs_add_child(parent_idx, new_idx);
    if (rc < 0) { ramfs_free_inode(new_idx); return rc; }
    
    if (out_idx) *out_idx = new_idx;
    return 0;
}

int ramfs_fill_stat(uint32_t inode_idx, struct stat* st) {
    if (inode_idx >= RAMFS_MAX_INODES || !st) return -1;
    
    ramfs_inode_t* inode = &inode_table[inode_idx];
    if (inode->type == RAMFS_INODE_FREE) return -1;
    
    memset(st, 0, sizeof(struct stat));
    st->st_ino     = inode_idx;
    st->st_mode    = inode->mode;
    st->st_mode   |= (inode->type == RAMFS_INODE_DIR) ? S_IFDIR : S_IFREG;
    st->st_nlink   = inode->nlink;
    st->st_size    = inode->size;
    st->st_blksize = RAMFS_BLOCK_SIZE;
    st->st_blocks  = (inode->size + 511) / 512;
    return 0;
}

void* ramfs_get_inode_table(void) {
    return inode_table;
}
