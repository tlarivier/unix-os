#include <kernel/hashtable.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>  

void hash_table_init(hash_table_t *table, const char *name) {
    if (!table) return;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        table->buckets[i] = NULL;
    }
    table->count = 0;
    table->name = name;
}

int hash_table_insert(hash_table_t *table, uint32_t key, void *value) {
    if (!table || !value) return -EINVAL;
    uint32_t bucket = hash_function(key);
    hash_entry_t *entry = kmalloc(sizeof(hash_entry_t));
    if (!entry) return -ENOMEM;
    entry->key   = key;
    entry->value = value;
    entry->next  = table->buckets[bucket];
    table->buckets[bucket] = entry;
    table->count++;
    return 0;
}

void *hash_table_lookup(hash_table_t *table, uint32_t key) {
    if (!table) return NULL;
    uint32_t bucket = hash_function(key);
    hash_entry_t *entry = table->buckets[bucket];
    while (entry) {
        if (entry->key == key) return entry->value;
        entry = entry->next;
    }
    return NULL;
}

void *hash_table_remove(hash_table_t *table, uint32_t key) {
    if (!table) return NULL;
    uint32_t bucket = hash_function(key);
    hash_entry_t **entry_ptr = &table->buckets[bucket];
    hash_entry_t *entry = *entry_ptr;
    while (entry) {
        if (entry->key == key) {
            *entry_ptr = entry->next;
            void *result = entry->value;
            kfree(entry);
            table->count--;
            return result;
        }
        entry_ptr = &entry->next;
        entry = entry->next;
    }
    return NULL;
}

void hash_table_destroy(hash_table_t *table) {
    if (!table) return;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        hash_entry_t *entry = table->buckets[i];
        while (entry) {
            hash_entry_t *next = entry->next;
            kfree(entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }
    table->count = 0;
}

uint32_t hash_table_size(hash_table_t *table) {
    return table ? table->count : 0;
}

void hash_table_stats(hash_table_t *table) {
    if (!table) return;
    kprintf("Hash '%s': %u entries\n", table->name, table->count);
}
