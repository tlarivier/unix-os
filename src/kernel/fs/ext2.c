/*
 * ext2.c — ext2 rev-1 driver over a block_device_t. Read path + a
 * journal-backed write path that is armed for the lifetime of the mount.
 *
 * Invariants:
 *  - Lock order is ext2_lock -> g_journal_lock; never the reverse.
 *  - Post-mount geometry (g_sb, g_gdt, g_block_size, ...) is immutable.
 *  - g_block_buf is per-CPU scratch; accesses pin preemption via this_cpu().
 *  - g_write_window_open transitions 0 -> 1 inside ext2_mount() on success
 *    and stays 1 for the lifetime of the mount. journaled_write gates on it
 *    AND requires the journal to be initialised; otherwise the call returns
 *    -EIO instead of panicking.
 *
 * Not allowed:
 *  - Calling schedule() / sleeping while ext2_lock is held.
 *  - Exposing on-disk struct/macro layout outside this directory.
 */

#include "ext2_ondisk.h"
#include <kernel/block.h>
#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/journal.h>
#include <kernel/kernel.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/preempt.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define EXT2_MAX_GROUPS 64 /* enough for a 1 GiB fs at b=4096 */

static block_device_t *g_bd = 0;
static ext2_superblock_t g_sb;
static ext2_group_desc_t g_gdt[EXT2_MAX_GROUPS];
static uint32_t g_block_size = 0;
static uint32_t g_inode_size = 0;
static uint32_t g_blocks_per_group = 0;
static uint32_t g_inodes_per_group = 0;
static uint32_t g_groups = 0;
static uint32_t g_sectors_per_blk = 0;
static int g_dirty_sb = 0;
static int g_dirty_gdt = 0;

static int g_write_window_open = 0;

static spinlock_t ext2_lock = SPINLOCK_INIT("ext2");

#include <kernel/percpu.h>
#define g_block_buf (this_cpu()->block_buf)

static void ext2_sync_locked(void);

int ext2_is_mounted(void) { return g_bd != 0; }

static int journaled_write(uint32_t blk, const void *buf);

static int read_block(uint32_t blk, void *buf) {
  return blk_read(g_bd, (uint64_t)blk * g_sectors_per_blk, g_sectors_per_blk,
                  buf);
}

int ext2_reserve_blocks(uint32_t start, uint32_t count) {
  if (!g_bd)
    return 0;
  spin_lock(&ext2_lock);
  int reserved = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t blk = start + i;
    if (blk < g_sb.s_first_data_block)
      continue;
    uint32_t rel = blk - g_sb.s_first_data_block;
    uint32_t group = rel / g_blocks_per_group;
    uint32_t bit = rel % g_blocks_per_group;
    if (group >= g_groups)
      continue;
    if (read_block(g_gdt[group].bg_block_bitmap, g_block_buf) != 0)
      continue;
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (g_block_buf[bit >> 3] & mask)
      continue; /* already reserved */
    g_block_buf[bit >> 3] |= mask;
    if (journaled_write(g_gdt[group].bg_block_bitmap, g_block_buf) != 0)
      continue;
    g_gdt[group].bg_free_blocks_count--;
    g_sb.s_free_blocks_count--;
    g_dirty_gdt = 1;
    g_dirty_sb = 1;
    reserved++;
  }
  if (reserved > 0)
    ext2_sync_locked();
  spin_unlock(&ext2_lock);
  return reserved;
}

static int journaled_write(uint32_t blk, const void *buf) {
  if (!g_bd || !g_write_window_open)
    return -EIO;
  journal_txn_t *t = journal_begin();
  if (!t)
    return -EIO;
  int rc = journal_log_block(t, blk, buf);
  if (rc < 0) {
    journal_abort(t);
    return rc;
  }
  return journal_commit(t);
}

static void recompute_geometry(void) {
  g_block_size = 1024u << g_sb.s_log_block_size;
  g_inode_size = (g_sb.s_rev_level >= 1) ? g_sb.s_inode_size : 128;
  g_blocks_per_group = g_sb.s_blocks_per_group;
  g_inodes_per_group = g_sb.s_inodes_per_group;
  g_sectors_per_blk = g_block_size / 512;
  g_groups =
      (g_sb.s_blocks_count + g_blocks_per_group - 1) / g_blocks_per_group;
}

int ext2_mount(block_device_t *bd) {
  if (!bd)
    return -EINVAL;
  spin_lock(&ext2_lock);
  g_bd = bd;

  uint8_t sb_buf[1024];
  if (blk_read(bd, 2, 2, sb_buf) != 0) {
    g_bd = 0;
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  kmemcpy(&g_sb, sb_buf, sizeof(g_sb) < 1024 ? sizeof(g_sb) : 1024);
  if (g_sb.s_magic != EXT2_MAGIC) {
    kprintf("ext2: bad magic %x (expected %x)\n", g_sb.s_magic, EXT2_MAGIC);
    g_bd = 0;
    spin_unlock(&ext2_lock);
    return -EINVAL;
  }

  recompute_geometry();
  if (g_block_size > sizeof(g_block_buf)) {
    kprintf("ext2: unsupported block size %x\n", g_block_size);
    g_bd = 0;
    spin_unlock(&ext2_lock);
    return -EINVAL;
  }
  if (g_groups > EXT2_MAX_GROUPS) {
    kprintf("ext2: too many groups (%x > %x)\n", g_groups, EXT2_MAX_GROUPS);
    g_bd = 0;
    spin_unlock(&ext2_lock);
    return -EINVAL;
  }

  uint32_t gdt_block = (g_block_size == 1024) ? 2 : 1;
  if (read_block(gdt_block, g_block_buf) != 0) {
    g_bd = 0;
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  uint32_t gdt_bytes = g_groups * sizeof(ext2_group_desc_t);
  if (gdt_bytes > g_block_size) {
    kmemcpy(g_gdt, g_block_buf, g_block_size);
    uint32_t remaining = gdt_bytes - g_block_size;
    uint8_t *dst = (uint8_t *)g_gdt + g_block_size;
    uint32_t nblocks = (remaining + g_block_size - 1) / g_block_size;
    for (uint32_t i = 0; i < nblocks; i++) {
      if (read_block(gdt_block + 1 + i, g_block_buf) != 0) {
        g_bd = 0;
        spin_unlock(&ext2_lock);
        return -EIO;
      }
      uint32_t copy = (remaining > g_block_size) ? g_block_size : remaining;
      kmemcpy(dst, g_block_buf, copy);
      dst += copy;
      remaining -= copy;
    }
  } else {
    kmemcpy(g_gdt, g_block_buf, gdt_bytes);
  }

  kprintf(
      "ext2: mounted bs=%x inode_size=%x groups=%x inodes/g=%x blocks/g=%x\n",
      g_block_size, g_inode_size, g_groups, g_inodes_per_group,
      g_blocks_per_group);
  journal_set_target_sectors(g_sectors_per_blk);
  g_write_window_open = 1;
  spin_unlock(&ext2_lock);
  return 0;
}

void ext2_print_info(void) {
  if (!g_bd) {
    kprintf("ext2: not mounted\n");
    return;
  }
  kprintf("ext2: total_blocks=%x free=%x total_inodes=%x free=%x\n",
          g_sb.s_blocks_count, g_sb.s_free_blocks_count, g_sb.s_inodes_count,
          g_sb.s_free_inodes_count);
}

static int read_inode(uint32_t ino, ext2_inode_t *out) {
  if (ino == 0 || ino > g_sb.s_inodes_count)
    return -ENOENT;
  uint32_t group = (ino - 1) / g_inodes_per_group;
  uint32_t index = (ino - 1) % g_inodes_per_group;
  if (group >= g_groups)
    return -EIO;
  uint32_t off = index * g_inode_size;
  uint32_t inblock = off / g_block_size;
  uint32_t inoff = off % g_block_size;

  if (read_block(g_gdt[group].bg_inode_table + inblock, g_block_buf) != 0)
    return -EIO;
  kmemcpy(out, g_block_buf + inoff,
          sizeof(ext2_inode_t) < g_inode_size ? sizeof(ext2_inode_t)
                                              : g_inode_size);
  return 0;
}

static uint32_t inode_block_phys(const ext2_inode_t *in, uint32_t logical) {
  uint32_t ptrs_per_block = g_block_size / sizeof(uint32_t);

  if (logical < EXT2_NDIR_BLOCKS) {
    return in->i_block[logical];
  }
  logical -= EXT2_NDIR_BLOCKS;

  if (logical < ptrs_per_block) {
    uint32_t indir = in->i_block[EXT2_IND_BLOCK];
    if (!indir)
      return 0;
    if (read_block(indir, g_block_buf) != 0)
      return 0;
    uint32_t v;
    kmemcpy(&v, g_block_buf + logical * 4, 4);
    return v;
  }
  logical -= ptrs_per_block;

  if (logical < ptrs_per_block * ptrs_per_block) {
    uint32_t l1 = logical / ptrs_per_block;
    uint32_t l2 = logical % ptrs_per_block;
    uint32_t dind = in->i_block[EXT2_DIND_BLOCK];
    if (!dind)
      return 0;
    if (read_block(dind, g_block_buf) != 0)
      return 0;
    uint32_t mid;
    kmemcpy(&mid, g_block_buf + l1 * 4, 4);
    if (!mid)
      return 0;
    if (read_block(mid, g_block_buf) != 0)
      return 0;
    uint32_t v;
    kmemcpy(&v, g_block_buf + l2 * 4, 4);
    return v;
  }
  return 0;
}

static uint32_t dir_lookup_in_inode(const ext2_inode_t *dir, const char *name,
                                    uint32_t name_len) {
  uint32_t total = dir->i_size;
  uint32_t logical = 0;
  while (logical * g_block_size < total) {
    uint32_t phys = inode_block_phys(dir, logical);
    if (!phys)
      break;
    if (read_block(phys, g_block_buf) != 0)
      return 0;

    uint32_t off = 0;
    while (off < g_block_size) {
      ext2_dir_entry_t *de = (ext2_dir_entry_t *)(g_block_buf + off);
      if (de->rec_len == 0)
        break;
      if (de->inode != 0 && de->name_len == name_len) {
        const char *de_name = (const char *)(de + 1);
        int match = 1;
        for (uint32_t i = 0; i < name_len; i++) {
          if (de_name[i] != name[i]) {
            match = 0;
            break;
          }
        }
        if (match)
          return de->inode;
      }
      off += de->rec_len;
    }
    logical++;
  }
  return 0;
}

static int32_t read_inode_data(const ext2_inode_t *in, void *buf, uint32_t len,
                               uint32_t offset) {
  if (offset >= in->i_size)
    return 0;
  uint32_t to_read = (offset + len > in->i_size) ? (in->i_size - offset) : len;
  uint32_t got = 0;
  uint8_t *out = (uint8_t *)buf;

  while (got < to_read) {
    uint32_t pos = offset + got;
    uint32_t logical = pos / g_block_size;
    uint32_t inoff = pos % g_block_size;
    uint32_t take = g_block_size - inoff;
    if (take > to_read - got)
      take = to_read - got;

    uint32_t phys = inode_block_phys(in, logical);
    if (phys == 0) {
      for (uint32_t i = 0; i < take; i++)
        out[got + i] = 0;
    } else {
      if (read_block(phys, g_block_buf) != 0)
        return -EIO;
      kmemcpy(out + got, g_block_buf + inoff, take);
    }
    got += take;
  }
  return (int32_t)got;
}

int32_t ext2_read_inode(uint32_t ino, void *buf, uint32_t len,
                        uint32_t offset) {
  if (!g_bd)
    return -EINVAL;
  preempt_disable();
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    preempt_enable();
    return -EIO;
  }
  if ((in.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
    preempt_enable();
    return -EISDIR;
  }
  int32_t rc = read_inode_data(&in, buf, len, offset);
  preempt_enable();
  return rc;
}

static void ext2_sync_locked(void) {
  if (!g_bd)
    return;
  if (g_dirty_sb) {
    uint8_t sb_buf[1024];
    for (int i = 0; i < 1024; i++)
      sb_buf[i] = 0;
    kmemcpy(sb_buf, &g_sb, sizeof(g_sb) < 1024 ? sizeof(g_sb) : 1024);
    blk_write(g_bd, 2, 2, sb_buf);
    g_dirty_sb = 0;
  }
  if (g_dirty_gdt) {
    uint32_t gdt_block = (g_block_size == 1024) ? 2 : 1;
    uint32_t gdt_bytes = g_groups * sizeof(ext2_group_desc_t);
    uint32_t nblocks = (gdt_bytes + g_block_size - 1) / g_block_size;
    uint8_t *src = (uint8_t *)g_gdt;
    for (uint32_t i = 0; i < nblocks; i++) {
      for (uint32_t j = 0; j < g_block_size; j++)
        g_block_buf[j] = 0;
      uint32_t copy = (gdt_bytes > g_block_size) ? g_block_size : gdt_bytes;
      kmemcpy(g_block_buf, src, copy);
      journaled_write(gdt_block + i, g_block_buf);
      src += copy;
      gdt_bytes -= copy;
    }
    g_dirty_gdt = 0;
  }
}

static int write_inode_locked(uint32_t ino, const ext2_inode_t *in) {
  if (ino == 0 || ino > g_sb.s_inodes_count)
    return -EINVAL;
  uint32_t group = (ino - 1) / g_inodes_per_group;
  uint32_t index = (ino - 1) % g_inodes_per_group;
  if (group >= g_groups)
    return -EIO;
  uint32_t off = index * g_inode_size;
  uint32_t inblock = off / g_block_size;
  uint32_t inoff = off % g_block_size;
  uint32_t blk = g_gdt[group].bg_inode_table + inblock;

  if (read_block(blk, g_block_buf) != 0)
    return -EIO;
  uint32_t copy = sizeof(*in) < g_inode_size ? sizeof(*in) : g_inode_size;
  kmemcpy(g_block_buf + inoff, in, copy);
  return journaled_write(blk, g_block_buf);
}

static int alloc_block_in_group_locked(uint32_t group, uint32_t *out_block) {
  if (g_gdt[group].bg_free_blocks_count == 0)
    return -ENOSPC;
  if (read_block(g_gdt[group].bg_block_bitmap, g_block_buf) != 0)
    return -EIO;
  for (uint32_t bit = 0; bit < g_blocks_per_group; bit++) {
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (g_block_buf[bit >> 3] & mask)
      continue;
    g_block_buf[bit >> 3] |= mask;
    if (journaled_write(g_gdt[group].bg_block_bitmap, g_block_buf) != 0)
      return -EIO;
    g_gdt[group].bg_free_blocks_count--;
    g_sb.s_free_blocks_count--;
    g_dirty_gdt = 1;
    g_dirty_sb = 1;
    *out_block = g_sb.s_first_data_block + group * g_blocks_per_group + bit;
    return 0;
  }
  return -ENOSPC;
}

static int alloc_inode_in_group_locked(uint32_t group, uint32_t *out_ino) {
  if (g_gdt[group].bg_free_inodes_count == 0)
    return -ENOSPC;
  if (read_block(g_gdt[group].bg_inode_bitmap, g_block_buf) != 0)
    return -EIO;
  for (uint32_t bit = 0; bit < g_inodes_per_group; bit++) {
    uint8_t mask = (uint8_t)(1u << (bit & 7));
    if (g_block_buf[bit >> 3] & mask)
      continue;
    uint32_t ino = group * g_inodes_per_group + bit + 1;
    if (g_sb.s_rev_level >= 1 && ino < g_sb.s_first_ino)
      continue;
    g_block_buf[bit >> 3] |= mask;
    if (journaled_write(g_gdt[group].bg_inode_bitmap, g_block_buf) != 0)
      return -EIO;
    g_gdt[group].bg_free_inodes_count--;
    g_sb.s_free_inodes_count--;
    g_dirty_gdt = 1;
    g_dirty_sb = 1;
    *out_ino = ino;
    return 0;
  }
  return -ENOSPC;
}

int ext2_alloc_inode(uint16_t mode, uint32_t *out_ino) {
  if (!g_bd || !out_ino)
    return -EINVAL;
  spin_lock(&ext2_lock);
  uint32_t ino = 0;
  int rc = -ENOSPC;
  for (uint32_t g = 0; g < g_groups; g++) {
    rc = alloc_inode_in_group_locked(g, &ino);
    if (rc == 0)
      break;
    if (rc == -EIO)
      break;
  }
  if (rc != 0) {
    spin_unlock(&ext2_lock);
    return rc;
  }

  ext2_inode_t fresh;
  kmemset(&fresh, 0, sizeof(fresh));
  fresh.i_mode = mode;
  fresh.i_links_count = 1;
  fresh.i_size = 0;
  fresh.i_blocks = 0;
  uint32_t now = time_now_epoch_secs();
  fresh.i_ctime = now;
  fresh.i_mtime = now;
  fresh.i_atime = now;

  rc = write_inode_locked(ino, &fresh);
  if (rc != 0) {
    spin_unlock(&ext2_lock);
    return rc;
  }
  if ((mode & EXT2_S_IFMT) == EXT2_S_IFDIR) {
    uint32_t group = (ino - 1) / g_inodes_per_group;
    g_gdt[group].bg_used_dirs_count++;
    g_dirty_gdt = 1;
  }
  ext2_sync_locked();
  spin_unlock(&ext2_lock);
  *out_ino = ino;
  return 0;
}

static int splice_and_journal_locked(uint32_t phys, uint32_t inoff,
                                     const void *src, uint32_t len,
                                     int zero_first) {
  if (zero_first) {
    for (uint32_t i = 0; i < g_block_size; i++)
      g_block_buf[i] = 0;
  } else {
    if (read_block(phys, g_block_buf) != 0)
      return -EIO;
  }
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < len; i++)
    g_block_buf[inoff + i] = s[i];
  return journaled_write(phys, g_block_buf);
}

int ext2_inode_write_data(uint32_t ino, const void *buf, uint32_t count,
                          uint32_t offset) {
  if (!g_bd || !buf)
    return -EINVAL;
  if (count == 0)
    return 0;
  uint64_t end = (uint64_t)offset + count;
  if (end > (uint64_t)EXT2_NDIR_BLOCKS * g_block_size)
    return -EFBIG;

  spin_lock(&ext2_lock);
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  if ((in.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
    spin_unlock(&ext2_lock);
    return -EISDIR;
  }

  const uint8_t *src = (const uint8_t *)buf;
  uint32_t written = 0;
  int inode_dirty = 0;
  while (written < count) {
    uint32_t pos = offset + written;
    uint32_t logical = pos / g_block_size;
    uint32_t inoff = pos % g_block_size;
    uint32_t take = g_block_size - inoff;
    if (take > count - written)
      take = count - written;

    uint32_t phys = in.i_block[logical];
    int allocated = 0;
    if (phys == 0) {
      uint32_t new_blk = 0;
      int rc = alloc_block_in_group_locked(0, &new_blk);
      for (uint32_t g = 1; rc != 0 && g < g_groups; g++)
        rc = alloc_block_in_group_locked(g, &new_blk);
      if (rc != 0) {
        spin_unlock(&ext2_lock);
        return rc;
      }
      in.i_block[logical] = new_blk;
      in.i_blocks += g_block_size / 512;
      phys = new_blk;
      allocated = 1;
      inode_dirty = 1;
    }

    int rc =
        splice_and_journal_locked(phys, inoff, src + written, take, allocated);
    if (rc != 0) {
      spin_unlock(&ext2_lock);
      return rc;
    }
    written += take;
  }

  if (offset + count > in.i_size) {
    in.i_size = offset + count;
    inode_dirty = 1;
  }
  uint32_t now = time_now_epoch_secs();
  in.i_mtime = now;
  in.i_ctime = now;
  inode_dirty = 1;

  if (inode_dirty) {
    int rc = write_inode_locked(ino, &in);
    if (rc != 0) {
      spin_unlock(&ext2_lock);
      return rc;
    }
  }
  ext2_sync_locked();
  spin_unlock(&ext2_lock);
  return (int)written;
}

int ext2_inode_truncate(uint32_t ino, uint32_t new_size) {
  if (!g_bd)
    return -EINVAL;
  if (new_size > (uint32_t)(EXT2_NDIR_BLOCKS * g_block_size))
    return -EFBIG;

  spin_lock(&ext2_lock);
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }

  uint32_t keep_blocks = (new_size + g_block_size - 1) / g_block_size;
  if (keep_blocks > EXT2_NDIR_BLOCKS)
    keep_blocks = EXT2_NDIR_BLOCKS;
  for (uint32_t i = keep_blocks; i < EXT2_NDIR_BLOCKS; i++) {
    uint32_t phys = in.i_block[i];
    if (phys == 0)
      continue;
    if (phys >= g_sb.s_first_data_block) {
      uint32_t rel = phys - g_sb.s_first_data_block;
      uint32_t group = rel / g_blocks_per_group;
      uint32_t bit = rel % g_blocks_per_group;
      if (group < g_groups &&
          read_block(g_gdt[group].bg_block_bitmap, g_block_buf) == 0) {
        uint8_t mask = (uint8_t)(1u << (bit & 7));
        if (g_block_buf[bit >> 3] & mask) {
          g_block_buf[bit >> 3] &= (uint8_t)~mask;
          if (journaled_write(g_gdt[group].bg_block_bitmap, g_block_buf) == 0) {
            g_gdt[group].bg_free_blocks_count++;
            g_sb.s_free_blocks_count++;
            g_dirty_gdt = 1;
            g_dirty_sb = 1;
            if (in.i_blocks >= g_block_size / 512)
              in.i_blocks -= g_block_size / 512;
          }
        }
      }
    }
    in.i_block[i] = 0;
  }

  in.i_size = new_size;
  in.i_mtime = time_now_epoch_secs();
  in.i_ctime = in.i_mtime;

  int rc = write_inode_locked(ino, &in);
  if (rc == 0)
    ext2_sync_locked();
  spin_unlock(&ext2_lock);
  return rc;
}

static inline uint32_t dir_entry_size(uint32_t name_len) {
  uint32_t s = 8 + name_len;
  return (s + 3) & ~3u;
}

int ext2_dir_add_entry(uint32_t parent_ino, const char *name,
                       uint32_t child_ino, uint8_t file_type) {
  if (!g_bd || !name || !name[0] || child_ino == 0)
    return -EINVAL;
  uint32_t name_len = 0;
  while (name[name_len])
    name_len++;
  if (name_len > 255)
    return -ENAMETOOLONG;
  uint32_t need = dir_entry_size(name_len);

  spin_lock(&ext2_lock);
  ext2_inode_t dir;
  if (read_inode(parent_ino, &dir) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  if ((dir.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
    spin_unlock(&ext2_lock);
    return -ENOTDIR;
  }

  for (uint32_t logical = 0; logical < EXT2_NDIR_BLOCKS; logical++) {
    uint32_t phys = dir.i_block[logical];
    if (phys == 0)
      break;
    if (read_block(phys, g_block_buf) != 0) {
      spin_unlock(&ext2_lock);
      return -EIO;
    }

    uint32_t off = 0;
    while (off < g_block_size) {
      ext2_dir_entry_t *de = (ext2_dir_entry_t *)(g_block_buf + off);
      if (de->rec_len == 0)
        break;
      if (de->inode != 0 && de->name_len == name_len) {
        const char *de_name = (const char *)(de + 1);
        int match = 1;
        for (uint32_t i = 0; i < name_len; i++)
          if (de_name[i] != name[i]) {
            match = 0;
            break;
          }
        if (match) {
          spin_unlock(&ext2_lock);
          return -EEXIST;
        }
      }
      off += de->rec_len;
    }

    off = 0;
    while (off < g_block_size) {
      ext2_dir_entry_t *de = (ext2_dir_entry_t *)(g_block_buf + off);
      if (de->rec_len == 0)
        break;
      uint32_t actual = de->inode ? dir_entry_size(de->name_len) : 0;
      uint32_t slack = de->rec_len - actual;
      uint32_t next_off = off + de->rec_len;

      if (de->inode == 0 && de->rec_len >= need) {
        /* Reuse this empty record. */
        de->inode = child_ino;
        de->name_len = (uint8_t)name_len;
        de->file_type = file_type;
        char *dname = (char *)(de + 1);
        for (uint32_t i = 0; i < name_len; i++)
          dname[i] = name[i];
        if (journaled_write(phys, g_block_buf) != 0) {
          spin_unlock(&ext2_lock);
          return -EIO;
        }
        ext2_sync_locked();
        spin_unlock(&ext2_lock);
        return 0;
      }

      if (de->inode != 0 && slack >= need) {
        de->rec_len = (uint16_t)actual;
        ext2_dir_entry_t *ne = (ext2_dir_entry_t *)(g_block_buf + off + actual);
        ne->inode = child_ino;
        ne->rec_len = (uint16_t)(next_off - (off + actual));
        ne->name_len = (uint8_t)name_len;
        ne->file_type = file_type;
        char *dname = (char *)(ne + 1);
        for (uint32_t i = 0; i < name_len; i++)
          dname[i] = name[i];
        if (journaled_write(phys, g_block_buf) != 0) {
          spin_unlock(&ext2_lock);
          return -EIO;
        }
        ext2_sync_locked();
        spin_unlock(&ext2_lock);
        return 0;
      }

      off = next_off;
    }
  }

  uint32_t new_slot = EXT2_NDIR_BLOCKS;
  for (uint32_t i = 0; i < EXT2_NDIR_BLOCKS; i++) {
    if (dir.i_block[i] == 0) {
      new_slot = i;
      break;
    }
  }
  if (new_slot == EXT2_NDIR_BLOCKS) {
    spin_unlock(&ext2_lock);
    return -EFBIG;
  }

  uint32_t new_blk = 0;
  int rc = -ENOSPC;
  for (uint32_t g = 0; g < g_groups; g++) {
    rc = alloc_block_in_group_locked(g, &new_blk);
    if (rc == 0)
      break;
    if (rc == -EIO)
      break;
  }
  if (rc != 0) {
    spin_unlock(&ext2_lock);
    return rc;
  }

  /* Lay out a single entry that spans the whole new block. */
  for (uint32_t i = 0; i < g_block_size; i++)
    g_block_buf[i] = 0;
  ext2_dir_entry_t *de = (ext2_dir_entry_t *)g_block_buf;
  de->inode = child_ino;
  de->rec_len = (uint16_t)g_block_size;
  de->name_len = (uint8_t)name_len;
  de->file_type = file_type;
  char *dname = (char *)(de + 1);
  for (uint32_t i = 0; i < name_len; i++)
    dname[i] = name[i];
  if (journaled_write(new_blk, g_block_buf) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }

  dir.i_block[new_slot] = new_blk;
  dir.i_blocks += g_block_size / 512;
  dir.i_size += g_block_size;
  dir.i_mtime = time_now_epoch_secs();
  dir.i_ctime = dir.i_mtime;
  rc = write_inode_locked(parent_ino, &dir);
  if (rc == 0)
    ext2_sync_locked();
  spin_unlock(&ext2_lock);
  return rc;
}

int ext2_dir_remove_entry(uint32_t parent_ino, const char *name) {
  if (!g_bd || !name || !name[0])
    return -EINVAL;
  uint32_t name_len = 0;
  while (name[name_len])
    name_len++;

  spin_lock(&ext2_lock);
  ext2_inode_t dir;
  if (read_inode(parent_ino, &dir) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  if ((dir.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
    spin_unlock(&ext2_lock);
    return -ENOTDIR;
  }

  for (uint32_t logical = 0; logical < EXT2_NDIR_BLOCKS; logical++) {
    uint32_t phys = dir.i_block[logical];
    if (phys == 0)
      break;
    if (read_block(phys, g_block_buf) != 0) {
      spin_unlock(&ext2_lock);
      return -EIO;
    }

    uint32_t off = 0;
    uint32_t prev_off = (uint32_t)-1;
    while (off < g_block_size) {
      ext2_dir_entry_t *de = (ext2_dir_entry_t *)(g_block_buf + off);
      if (de->rec_len == 0)
        break;
      if (de->inode != 0 && de->name_len == name_len) {
        const char *de_name = (const char *)(de + 1);
        int match = 1;
        for (uint32_t i = 0; i < name_len; i++)
          if (de_name[i] != name[i]) {
            match = 0;
            break;
          }
        if (match) {
          if (prev_off == (uint32_t)-1) {
            de->inode = 0;
          } else {
            ext2_dir_entry_t *pe = (ext2_dir_entry_t *)(g_block_buf + prev_off);
            pe->rec_len = (uint16_t)(pe->rec_len + de->rec_len);
          }
          if (journaled_write(phys, g_block_buf) != 0) {
            spin_unlock(&ext2_lock);
            return -EIO;
          }
          dir.i_mtime = time_now_epoch_secs();
          dir.i_ctime = dir.i_mtime;
          int rc = write_inode_locked(parent_ino, &dir);
          if (rc == 0)
            ext2_sync_locked();
          spin_unlock(&ext2_lock);
          return rc;
        }
      }
      prev_off = off;
      off += de->rec_len;
    }
  }

  spin_unlock(&ext2_lock);
  return -ENOENT;
}

int ext2_inode_adjust_links(uint32_t ino, int delta) {
  if (!g_bd)
    return -EINVAL;
  spin_lock(&ext2_lock);
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    spin_unlock(&ext2_lock);
    return -EIO;
  }
  int32_t v = (int32_t)in.i_links_count + delta;
  if (v < 0)
    v = 0;
  if (v > 0xFFFF)
    v = 0xFFFF;
  in.i_links_count = (uint16_t)v;
  in.i_ctime = time_now_epoch_secs();
  int rc = write_inode_locked(ino, &in);
  if (rc == 0)
    ext2_sync_locked();
  spin_unlock(&ext2_lock);
  return rc;
}

uint32_t ext2_dir_lookup(uint32_t parent_ino, const char *name) {
  if (!g_bd || !name || !name[0])
    return 0;
  uint32_t name_len = 0;
  while (name[name_len])
    name_len++;

  preempt_disable();
  ext2_inode_t dir;
  if (read_inode(parent_ino, &dir) != 0) {
    preempt_enable();
    return 0;
  }
  if ((dir.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
    preempt_enable();
    return 0;
  }
  uint32_t found = dir_lookup_in_inode(&dir, name, name_len);
  preempt_enable();
  return found;
}

int ext2_stat(uint32_t ino, struct stat *st) {
  if (!g_bd || !st)
    return -EINVAL;
  preempt_disable();
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    preempt_enable();
    return -ENOENT;
  }
  kmemset(st, 0, sizeof(*st));
  st->st_ino = ino;
  st->st_mode = in.i_mode;
  st->st_nlink = in.i_links_count;
  st->st_uid = in.i_uid;
  st->st_gid = in.i_gid;
  st->st_size = in.i_size;
  st->st_blksize = g_block_size;
  st->st_blocks = in.i_blocks;
  st->st_atime = in.i_atime;
  st->st_mtime = in.i_mtime;
  st->st_ctime = in.i_ctime;
  preempt_enable();
  return 0;
}

uint8_t ext2_inode_dtype(uint32_t ino) {
  if (!g_bd)
    return 0; /* DT_UNKNOWN */
  preempt_disable();
  ext2_inode_t in;
  if (read_inode(ino, &in) != 0) {
    preempt_enable();
    return 0;
  }
  uint16_t mode = in.i_mode & EXT2_S_IFMT;
  preempt_enable();
  if (mode == EXT2_S_IFDIR)
    return 4; /* DT_DIR */
  if (mode == EXT2_S_IFREG)
    return 8; /* DT_REG */
  return 0;   /* DT_UNKNOWN */
}

ssize_t ext2_readdir_chunk(uint32_t parent_ino, void *buf, size_t size,
                           uint32_t *pos) {
  if (!g_bd || !buf || !pos)
    return -EINVAL;

  preempt_disable();
  ext2_inode_t dir;
  if (read_inode(parent_ino, &dir) != 0) {
    preempt_enable();
    return -EIO;
  }
  if ((dir.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
    preempt_enable();
    return -ENOTDIR;
  }

  uint8_t *out = (uint8_t *)buf;
  size_t written = 0;
  uint32_t cursor = *pos;
  const uint32_t total = dir.i_size;

  while (cursor < total) {
    uint32_t logical = cursor / g_block_size;
    uint32_t inblk = cursor % g_block_size;
    uint32_t phys = inode_block_phys(&dir, logical);
    if (!phys)
      break;
    if (read_block(phys, g_block_buf) != 0) {
      preempt_enable();
      return -EIO;
    }

    uint8_t local_block[4096];
    for (uint32_t i = 0; i < g_block_size; i++)
      local_block[i] = g_block_buf[i];

    while (inblk < g_block_size) {
      ext2_dir_entry_t *de = (ext2_dir_entry_t *)(local_block + inblk);
      if (de->rec_len == 0)
        break;

      uint32_t advance = de->rec_len;
      int emit = (de->inode != 0 && de->name_len > 0);
      if (emit) {
        const char *de_name = (const char *)(de + 1);
        if (de->name_len == 1 && de_name[0] == '.')
          emit = 0;
        else if (de->name_len == 2 && de_name[0] == '.' && de_name[1] == '.')
          emit = 0;
      }

      if (emit) {
        uint32_t name_len = de->name_len;
        if (name_len > 255)
          name_len = 255;
        size_t reclen =
            __builtin_offsetof(struct linux_dirent, d_name) + name_len + 1;
        reclen = (reclen + 3) & ~(size_t)3;

        if (written + reclen > size) {
          *pos = cursor;
          preempt_enable();
          return (ssize_t)written;
        }

        struct linux_dirent *ent = (struct linux_dirent *)(out + written);
        ent->d_ino = de->inode;
        ent->d_off = cursor + advance;
        ent->d_reclen = (uint16_t)reclen;
        const char *de_name = (const char *)(de + 1);
        for (uint32_t i = 0; i < name_len; i++)
          ent->d_name[i] = de_name[i];
        ent->d_name[name_len] = '\0';
        written += reclen;
      }

      inblk += advance;
      cursor += advance;
      if (advance == 0)
        break; /* defensive: malformed rec_len */
    }
  }

  *pos = cursor;
  preempt_enable();
  return (ssize_t)written;
}
