#ifndef KERNEL_HASHTABLE_H
#define KERNEL_HASHTABLE_H

#include <stddef.h>
#include <stdint.h>

#define HASH_TABLE_SIZE 1024
#define HASH_MASK (HASH_TABLE_SIZE - 1)

typedef struct hash_entry {
  uint32_t key;
  void *value;
  struct hash_entry *next;
} hash_entry_t;

typedef struct hash_table {
  hash_entry_t *buckets[HASH_TABLE_SIZE];
  uint32_t count;
  const char *name;
} hash_table_t;

void hash_table_init(hash_table_t *table, const char *name);

int hash_table_insert(hash_table_t *table, uint32_t key, void *value,
                      hash_entry_t *entry);

void *hash_table_lookup(hash_table_t *table, uint32_t key);

void *hash_table_remove(hash_table_t *table, uint32_t key,
                        hash_entry_t **out_entry);

static inline uint32_t hash_function(uint32_t key) {
  uint32_t hash = key;
  hash += (hash << 10);
  hash ^= (hash >> 6);
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash & HASH_MASK;
}

#endif
