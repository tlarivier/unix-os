/*
 * hashtable.c — generic chained hash table with caller-owned entries
 *               (hash_entry_t embedded in caller structs); duplicates
 *               rejected with -EEXIST.
 *
 * Invariants:
 *  - Fixed table size HASH_TABLE_SIZE=1024; linear chaining per bucket.
 *  - No internal lock — caller must provide mutual exclusion across
 *    init/insert/lookup/remove on the same table.
 *  - value == NULL is an invalid sentinel on insert (returns -EINVAL);
 *    NULL cannot be stored.
 *  - Insert scans the bucket and returns -EEXIST on duplicate key
 *    (no silent last-write-wins).
 *
 * Not allowed:
 *  - Allocating an entry node internally — entries are caller-owned.
 *  - Holding a caller spinlock across any path that would call kmalloc
 *    (callers must pre-allocate hash_entry_t before locking).
 */

#include <kernel/errno.h>
#include <kernel/hashtable.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>

void hash_table_init(hash_table_t *table, const char *name) {
  if (!table)
    return;
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    table->buckets[i] = NULL;
  }
  table->count = 0;
  table->name = name;
}

int hash_table_insert(hash_table_t *table, uint32_t key, void *value,
                      hash_entry_t *entry) {
  if (!table || !value || !entry)
    return -EINVAL;
  uint32_t bucket = hash_function(key);
  for (hash_entry_t *e = table->buckets[bucket]; e; e = e->next) {
    if (e->key == key)
      return -EEXIST;
  }
  entry->key = key;
  entry->value = value;
  entry->next = table->buckets[bucket];
  table->buckets[bucket] = entry;
  table->count++;
  return 0;
}

void *hash_table_lookup(hash_table_t *table, uint32_t key) {
  if (!table)
    return NULL;
  uint32_t bucket = hash_function(key);
  hash_entry_t *entry = table->buckets[bucket];
  while (entry) {
    if (entry->key == key)
      return entry->value;
    entry = entry->next;
  }
  return NULL;
}

void *hash_table_remove(hash_table_t *table, uint32_t key,
                        hash_entry_t **out_entry) {
  if (out_entry)
    *out_entry = NULL;
  if (!table)
    return NULL;
  uint32_t bucket = hash_function(key);
  hash_entry_t **entry_ptr = &table->buckets[bucket];
  hash_entry_t *entry = *entry_ptr;
  while (entry) {
    if (entry->key == key) {
      *entry_ptr = entry->next;
      void *result = entry->value;
      if (out_entry)
        *out_entry = entry;
      table->count--;
      return result;
    }
    entry_ptr = &entry->next;
    entry = entry->next;
  }
  return NULL;
}
