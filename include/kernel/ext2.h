#ifndef KERNEL_EXT2_H
#define KERNEL_EXT2_H

#include <kernel/block.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

struct stat;
struct fs_ops;

int ext2_mount(block_device_t *bd);
int ext2_is_mounted(void);
int ext2_reserve_blocks(uint32_t start, uint32_t count);
void ext2_print_info(void);
int32_t ext2_read_inode(uint32_t ino, void *buf, uint32_t len, uint32_t offset);
uint32_t ext2_dir_lookup(uint32_t parent_ino, const char *name);
int ext2_stat(uint32_t ino, struct stat *st);
uint8_t ext2_inode_dtype(uint32_t ino);
ssize_t ext2_readdir_chunk(uint32_t parent_ino, void *buf, size_t size,
                           uint32_t *pos);
int ext2_alloc_inode(uint16_t mode, uint32_t *out_ino);
int ext2_inode_write_data(uint32_t ino, const void *buf, uint32_t count,
                          uint32_t offset);
int ext2_inode_truncate(uint32_t ino, uint32_t new_size);
int ext2_dir_add_entry(uint32_t parent_ino, const char *name,
                       uint32_t child_ino, uint8_t file_type);
int ext2_dir_remove_entry(uint32_t parent_ino, const char *name);
int ext2_inode_adjust_links(uint32_t ino, int delta);

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2

extern const struct fs_ops ext2_ops;

#endif
