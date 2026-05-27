#ifndef KERNEL_JOURNAL_H
#define KERNEL_JOURNAL_H

#include <kernel/block.h>
#include <stddef.h>
#include <stdint.h>

#define JOURNAL_MAGIC 0x4A4F5552u
#define JOURNAL_VERSION 1
#define JOURNAL_BLOCK_SIZE 4096
#define JOURNAL_DEFAULT_BLOCKS 32
#define JOURNAL_MAX_PER_TXN 8

typedef struct journal_superblock {
  uint32_t magic;
  uint32_t version;
  uint32_t num_blocks;
  uint32_t txn_id_next;
  uint8_t pad[4080];
} journal_superblock_t;

_Static_assert(sizeof(journal_superblock_t) == JOURNAL_BLOCK_SIZE,
               "journal_superblock_t must fit in one 4KB block");

typedef struct journal_txn_header {
  uint32_t magic;
  uint32_t txn_id;
  uint32_t num_blocks;
  uint32_t committed;
  uint32_t target_blocks[JOURNAL_MAX_PER_TXN];
  uint32_t crc;
  uint8_t pad[JOURNAL_BLOCK_SIZE - 4 * 5 - 4 * JOURNAL_MAX_PER_TXN];
} journal_txn_header_t;

_Static_assert(sizeof(journal_txn_header_t) == JOURNAL_BLOCK_SIZE,
               "journal_txn_header_t must fit in one 4KB block");

typedef struct journal_txn journal_txn_t;

int journal_init(block_device_t *bd, uint32_t journal_start_block);
void journal_set_target_sectors(uint32_t target_sectors);
int journal_replay(void);
journal_txn_t *journal_begin(void);
int journal_log_block(journal_txn_t *txn, uint32_t target_block,
                      const void *data);
int journal_commit(journal_txn_t *txn);
void journal_abort(journal_txn_t *txn);

#endif
