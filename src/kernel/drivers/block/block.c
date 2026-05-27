/*
 * block.c — Named registry of block devices exposing a driver-agnostic
 * read/write/flush interface looked up by short name ("vda", "hda", ...).
 *
 * Invariants:
 *  - Registered block_device_t* live forever; names are unique in the table.
 *  - read_blocks/write_blocks are idempotent from the caller's view: the same
 *    (lba, count) may be retried without external side effects.
 *  - The registry is populated at boot (BSP, pre-SMP) and only read afterwards.
 *
 * Not allowed:
 *  - Calling vfs_*, schedule(), or any wait/signal primitive from here.
 *  - Exposing internal driver state (virtqueues, ring buffers) to FS callers.
 */

#include <kernel/block.h>
#include <kernel/errno.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>

#define MAX_BLOCK_DEVICES 8

static block_device_t *g_devs[MAX_BLOCK_DEVICES];
static int g_count;

int block_device_register(block_device_t *bd) {
  if (!bd || !bd->read_blocks || !bd->write_blocks)
    return -EINVAL;
  if (g_count >= MAX_BLOCK_DEVICES)
    return -ENOSPC;
  for (int i = 0; i < g_count; i++) {
    if (kstrcmp(g_devs[i]->name, bd->name) == 0)
      return -EEXIST;
  }
  g_devs[g_count++] = bd;
  kprintf("block: registered /dev/%s (block_size=%x total_blocks=%x)\n",
          bd->name, bd->block_size, (uint32_t)bd->total_blocks);
  return 0;
}

block_device_t *block_device_find(const char *name) {
  for (int i = 0; i < g_count; i++) {
    if (kstrcmp(g_devs[i]->name, name) == 0)
      return g_devs[i];
  }
  return 0;
}
