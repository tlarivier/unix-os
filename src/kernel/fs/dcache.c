/*
 * dcache.c — RCU-protected path-to-inode cache (32 slots, full-path
 * compare); currently exposes init/lookup/insert/invalidate with only
 * invalidate wired by vfs_path's unlink/rename/rmdir.
 *
 * Invariants:
 *  - g_dcache_lock serializes slot mutation; readers use rcu_read_lock +
 *    rcu_dereference and never take the lock.
 *  - Publication is rcu_assign_pointer so readers observe a fully
 *    initialized dentry once they see the new pointer.
 *  - Eviction frees the old pointer only after synchronize_rcu has
 *    quiesced concurrent readers.
 *
 * Not allowed:
 *  - Calling synchronize_rcu while holding vfs_lock (callers MUST drop
 *    vfs_lock before dcache_invalidate).
 *  - Allocating/freeing dentries from IRQ context.
 *  - Exposing dentry_t storage outside this TU.
 */

#include <kernel/dcache.h>
#include <kernel/memory.h>
#include <kernel/rcu.h>
#include <kernel/spinlock.h>

static dentry_t *g_slots[DCACHE_MAX_ENTRIES];
static spinlock_t g_dcache_lock = SPINLOCK_INIT("dcache");

static int path_eq(const char *a, const char *b) {
  for (size_t i = 0; i <= DCACHE_MAX_PATH_LEN; i++) {
    if (a[i] != b[i])
      return 0;
    if (a[i] == 0)
      return 1;
  }
  return 0;
}

void dcache_init(void) {
  for (int i = 0; i < DCACHE_MAX_ENTRIES; i++)
    g_slots[i] = NULL;
}

void dcache_invalidate(const char *path) {
  if (!path)
    return;
  dentry_t *old = NULL;
  spin_lock(&g_dcache_lock);
  for (int i = 0; i < DCACHE_MAX_ENTRIES; i++) {
    if (g_slots[i] && path_eq(g_slots[i]->path, path)) {
      old = g_slots[i];
      rcu_assign_pointer(g_slots[i], (dentry_t *)NULL);
      break;
    }
  }
  spin_unlock(&g_dcache_lock);

  if (old) {
    synchronize_rcu();
    kfree(old);
  }
}
