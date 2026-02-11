#ifndef _KERNEL_RAMFS_H
#define _KERNEL_RAMFS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>

typedef int32_t ssize_t;

#define RAMFS_MAX_INODES      128
#define RAMFS_MAX_CHILDREN    64
#define RAMFS_MAX_NAME        64
#define RAMFS_BLOCK_SIZE      4096
#define RAMFS_MAX_FILE_SIZE   (16 * 1024 * 1024)

#define RAMFS_INODE_FREE      0
#define RAMFS_INODE_FILE      1
#define RAMFS_INODE_DIR       2

typedef struct ramfs_inode {
    uint32_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t nlink;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    char name[RAMFS_MAX_NAME];
    uint32_t parent;
    uint16_t children[RAMFS_MAX_CHILDREN];
    uint32_t child_count;
    
    /* Data storage - simple contiguous buffer */
    uint8_t* data;
    uint32_t data_capacity;
} ramfs_inode_t;

int ramfs_init(void);
int ramfs_is_initialized(void);
spinlock_t* ramfs_get_lock(void);

uint32_t ramfs_alloc_inode(void);
void ramfs_free_inode(uint32_t idx);
ramfs_inode_t* ramfs_get_inode(uint32_t idx);
uint32_t ramfs_get_max_inodes(void);
uint32_t ramfs_get_inode_count(void);
void* ramfs_get_inode_table(void);

uint32_t ramfs_lookup_child(uint32_t parent, const char* name);
int ramfs_add_child(uint32_t parent, uint32_t child);
int ramfs_remove_child(uint32_t parent, uint32_t child);

uint32_t ramfs_path_to_inode(const char* path);

ssize_t ramfs_read_data(uint32_t inode_idx, void* buf, size_t count, uint32_t offset);
ssize_t ramfs_write_data(uint32_t inode_idx, const void* buf, size_t count, uint32_t offset);
int ramfs_truncate_inode(uint32_t inode_idx, uint32_t length);

int ramfs_create_dir(uint32_t parent_idx, const char* name, uint32_t mode, uint32_t* out_idx);
int ramfs_create_file(uint32_t parent_idx, const char* name, uint32_t mode, uint32_t* out_idx);

struct stat;
int ramfs_fill_stat(uint32_t inode_idx, struct stat* st);

#endif
