#ifndef KV_HASH_H
#define KV_HASH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include "kv_ioctl.h"


typedef u64 hash_key_t;

struct kv_hash_entry {
	hash_key_t key;
	char *value;
	struct hlist_node node;
};

struct hash_table {
	struct hlist_head *buckets;
  spinlock_t *bucket_locks;
	u32 bits;
};

/**
 * kv_hash_init - Initialize a hash table
 * @ht: Pointer to hash_table structure
 * @bits: Number of bits for bucket count (2^bits buckets)
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_init(struct hash_table *ht, u32 bits);

/**
 * kv_hash_destroy - Destroy a hash table and free all associated memory
 * @ht: Pointer to hash_table structure
 */
void kv_hash_destroy(struct hash_table *ht);

/**
 * kv_hash_add - Add or update a key-value pair in the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to add or update
 * @value: Value associated with the key
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_add(struct hash_table *ht, hash_key_t key, const char *value);

/**
 * kv_hash_remove - Remove a key-value pair from the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to remove
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_remove(struct hash_table *ht, hash_key_t key);

/**
 * kv_hash_lookup - Lookup a value by key in the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to lookup
 * @out_buf: Buffer to store the found value
 * @out_buflen: Length of the output buffer
 * Returns number of bytes copied on success, negative error code on failure
 */
ssize_t kv_hash_lookup(struct hash_table *ht, hash_key_t key, char *out_buf, size_t out_buflen);

/**
 * kv_hash_snapshot - Get a snapshot of key-value pairs in the hash table
 * @ht: Pointer to hash_table structure
 * @entries: Output array to store key-value pairs (caller-allocated)
 * @max_entries: Maximum number of entries that can be stored in the output array
 * @out_count: Output parameter to store the actual number of entries returned
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_snapshot(struct hash_table *ht, struct kv_message *entries, u32 max_entries, u32 *out_count);

/**
 * kv_hash_count - Get the count of key-value pairs in the hash table
 * @ht: Pointer to hash_table structure
 * Returns count on success, negative error code on failure
 */
int kv_hash_count(struct hash_table *ht);

#endif /* KV_HASH_H */
