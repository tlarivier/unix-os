/*
 * journal.c — single-slot write-ahead journal for ext2 metadata writes:
 * begin -> log_block -> commit -> apply, with idempotent replay at mount.
 *
 * Invariants:
 *  - At most one transaction is in flight; journal_begin spins until free.
 *  - Commit is synchronous: header + data persisted (with FLUSH) before apply.
 *  - CRC32 over the journal header guards torn writes; replay is idempotent.
 *  - g_journal_lock serializes begin/commit/replay even though ext2_lock
 *    already serializes producers (defense in depth).
 *  - Caller chain: ext2_lock held -> journal_begin -> ... -> journal_commit.
 *
 * Not allowed:
 *  - Calling schedule()/sleep while g_journal_lock is held.
 *  - Bypassing the journal for any ext2 metadata block write.
 *  - Running before journal_init(); replay must precede the first begin.
 */

#include <kernel/block.h>
#include <kernel/errno.h>
#include <kernel/journal.h>
#include <kernel/kernel.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <stddef.h>
#include <stdint.h>

static block_device_t *g_bd;
static uint32_t g_journal_start;
static uint32_t g_sectors_per_block;
static uint32_t g_target_sectors;
static journal_superblock_t g_sb;
static int g_initialized;

static spinlock_t g_journal_lock = SPINLOCK_INIT("journal");

#define JOURNAL_SLOT_HDR 1
#define JOURNAL_SLOT_DATA0 (JOURNAL_SLOT_HDR + 1)

struct journal_txn {
  int in_use;
  uint32_t txn_id;
  uint32_t num_blocks;
  uint32_t target_blocks[JOURNAL_MAX_PER_TXN];
  void *data_buffers[JOURNAL_MAX_PER_TXN];
};

static struct journal_txn g_txn;

static const uint8_t zero_block[JOURNAL_BLOCK_SIZE];

static uint32_t journal_crc(const journal_txn_header_t *h) {
  const uint8_t *bytes = (const uint8_t *)h;
  size_t end = (size_t)((const uint8_t *)&h->crc - bytes);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < end; i++) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; b++) {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

static int journal_write_block(uint32_t idx, const void *data) {
  uint64_t lba = (uint64_t)(g_journal_start + idx) * g_sectors_per_block;
  return blk_write(g_bd, lba, g_sectors_per_block, data);
}

static int journal_format_fresh(uint32_t num_blocks) {
  static uint8_t zero[JOURNAL_BLOCK_SIZE];
  for (uint32_t i = 1; i < num_blocks; i++) {
    int rc = journal_write_block(i, zero);
    if (rc < 0)
      return rc;
  }

  kmemset(&g_sb, 0, sizeof(g_sb));
  g_sb.magic = JOURNAL_MAGIC;
  g_sb.version = JOURNAL_VERSION;
  g_sb.num_blocks = num_blocks;
  g_sb.txn_id_next = 1;

  return journal_write_block(0, &g_sb);
}

int journal_init(block_device_t *bd, uint32_t journal_start_block) {
  if (!bd)
    return -EINVAL;

  g_bd = bd;
  g_journal_start = journal_start_block;
  g_sectors_per_block = JOURNAL_BLOCK_SIZE / 512;
  if (g_target_sectors == 0)
    g_target_sectors = g_sectors_per_block;

  uint64_t sb_lba = (uint64_t)g_journal_start * g_sectors_per_block;
  int rc = blk_read(g_bd, sb_lba, g_sectors_per_block, &g_sb);
  if (rc < 0) {
    kprintf("journal: read sb failed: %d\n", rc);
    return rc;
  }

  if (g_sb.magic == JOURNAL_MAGIC && g_sb.version == JOURNAL_VERSION) {
    kprintf("journal: existing magic OK, num_blocks=%u next_txn=%u\n",
            g_sb.num_blocks, g_sb.txn_id_next);
  } else {
    kprintf("journal: formatting fresh (%u blocks)\n", JOURNAL_DEFAULT_BLOCKS);
    rc = journal_format_fresh(JOURNAL_DEFAULT_BLOCKS);
    if (rc < 0) {
      kprintf("journal: format failed: %d\n", rc);
      return rc;
    }
  }

  kmemset(&g_txn, 0, sizeof(g_txn));

  g_initialized = 1;
  return 0;
}

static int discard_slot(uint32_t hdr_blk) {
  return journal_write_block(hdr_blk, zero_block);
}

int journal_replay(void) {
  if (!g_initialized)
    return -EINVAL;

  uint32_t hdr_blk = JOURNAL_SLOT_HDR;
  uint32_t data_blk0 = JOURNAL_SLOT_DATA0;

  journal_txn_header_t hdr;
  uint64_t hdr_lba =
      (uint64_t)(g_journal_start + hdr_blk) * g_sectors_per_block;
  int rc = blk_read(g_bd, hdr_lba, g_sectors_per_block, &hdr);
  if (rc < 0)
    return rc;

  if (hdr.magic != JOURNAL_MAGIC || hdr.committed != 1)
    return 0;

  uint32_t expected_crc = hdr.crc;
  hdr.crc = 0;
  uint32_t got = journal_crc(&hdr);
  if (got != expected_crc) {
    kprintf("journal_replay: CRC mismatch (got %x expected %x), discarding\n",
            got, expected_crc);
    return discard_slot(hdr_blk);
  }

  if (hdr.num_blocks == 0 || hdr.num_blocks > JOURNAL_MAX_PER_TXN) {
    kprintf("journal_replay: invalid num_blocks=%u\n", hdr.num_blocks);
    return discard_slot(hdr_blk);
  }

  kprintf("journal_replay: redoing txn_id=%u with %u blocks\n", hdr.txn_id,
          hdr.num_blocks);

  static uint8_t data_buf[JOURNAL_BLOCK_SIZE];
  for (uint32_t i = 0; i < hdr.num_blocks; i++) {
    uint64_t src_lba =
        (uint64_t)(g_journal_start + data_blk0 + i) * g_sectors_per_block;
    rc = blk_read(g_bd, src_lba, g_sectors_per_block, data_buf);
    if (rc < 0)
      return rc;
    uint64_t target_lba = (uint64_t)hdr.target_blocks[i] * g_target_sectors;
    rc = blk_write(g_bd, target_lba, g_target_sectors, data_buf);
    if (rc < 0)
      return rc;
  }

  journal_write_block(hdr_blk, zero_block);
  kprintf("journal_replay: txn_id=%u applied OK\n", hdr.txn_id);
  return 0;
}

journal_txn_t *journal_begin(void) {
  if (!g_initialized)
    return NULL;

  for (;;) {
    spin_lock(&g_journal_lock);
    if (!g_txn.in_use) {
      g_txn.in_use = 1;
      g_txn.txn_id = g_sb.txn_id_next++;
      g_txn.num_blocks = 0;
      spin_unlock(&g_journal_lock);
      return &g_txn;
    }
    spin_unlock(&g_journal_lock);
    __asm__ volatile("pause" ::: "memory");
  }
}

int journal_log_block(journal_txn_t *txn, uint32_t target_block,
                      const void *data) {
  if (!txn || !data)
    return -EINVAL;
  if (txn->num_blocks >= JOURNAL_MAX_PER_TXN)
    return -ENOMEM;

  void *buf = kmalloc(JOURNAL_BLOCK_SIZE);
  if (!buf)
    return -ENOMEM;
  kmemcpy(buf, data, JOURNAL_BLOCK_SIZE);

  int idx = txn->num_blocks;
  txn->target_blocks[idx] = target_block;
  txn->data_buffers[idx] = buf;
  txn->num_blocks++;
  return 0;
}

int journal_commit(journal_txn_t *txn) {
  if (!txn || !txn->in_use)
    return -EINVAL;
  if (txn->num_blocks > JOURNAL_MAX_PER_TXN)
    return -EINVAL;
  uint32_t hdr_blk = JOURNAL_SLOT_HDR;
  uint32_t data_blk0 = JOURNAL_SLOT_DATA0;

  journal_txn_header_t hdr;
  kmemset(&hdr, 0, sizeof(hdr));
  hdr.magic = JOURNAL_MAGIC;
  hdr.txn_id = txn->txn_id;
  hdr.num_blocks = txn->num_blocks;
  hdr.committed = 0;
  for (uint32_t i = 0; i < txn->num_blocks; i++) {
    hdr.target_blocks[i] = txn->target_blocks[i];
  }

  for (uint32_t i = 0; i < txn->num_blocks; i++) {
    int rc = journal_write_block(data_blk0 + i, txn->data_buffers[i]);
    if (rc < 0) {
      kprintf("journal_commit: data block %u write failed: %d\n", i, rc);
      journal_abort(txn);
      return rc;
    }
  }

  hdr.crc = 0;
  hdr.crc = journal_crc(&hdr);
  int rc = journal_write_block(hdr_blk, &hdr);
  if (rc < 0) {
    kprintf("journal_commit: pending header write failed: %d\n", rc);
    journal_abort(txn);
    return rc;
  }
  blk_flush(g_bd);

  hdr.committed = 1;
  hdr.crc = 0;
  hdr.crc = journal_crc(&hdr);
  rc = journal_write_block(hdr_blk, &hdr);
  if (rc < 0) {
    kprintf("journal_commit: committed header write failed: %d\n", rc);
    journal_abort(txn);
    return rc;
  }
  blk_flush(g_bd);

  for (uint32_t i = 0; i < txn->num_blocks; i++) {
    uint64_t target_lba = (uint64_t)txn->target_blocks[i] * g_target_sectors;
    rc = blk_write(g_bd, target_lba, g_target_sectors, txn->data_buffers[i]);
    if (rc < 0) {
      kprintf("journal_commit: apply target %u failed: %d "
              "(committed but not applied; replay will retry)\n",
              txn->target_blocks[i], rc);
      kernel_panic("journal apply failure post-commit", __FILE__, __LINE__);
    }
  }
  blk_flush(g_bd);

  rc = journal_write_block(hdr_blk, zero_block);
  if (rc < 0) {
    kprintf("journal_commit: header clear failed (harmless): %d\n", rc);
  }

  spin_lock(&g_journal_lock);
  if (txn->txn_id + 1 > g_sb.txn_id_next) {
    g_sb.txn_id_next = txn->txn_id + 1;
  }
  journal_write_block(0, &g_sb);
  spin_unlock(&g_journal_lock);

  journal_abort(txn);
  return 0;
}

void journal_set_target_sectors(uint32_t target_sectors) {
  if (target_sectors == 0)
    return;
  g_target_sectors = target_sectors;
}

void journal_abort(journal_txn_t *txn) {
  if (!txn)
    return;
  spin_lock(&g_journal_lock);
  for (uint32_t i = 0; i < txn->num_blocks; i++) {
    if (txn->data_buffers[i]) {
      kfree(txn->data_buffers[i]);
      txn->data_buffers[i] = NULL;
    }
  }
  txn->num_blocks = 0;
  txn->in_use = 0;
  spin_unlock(&g_journal_lock);
}
