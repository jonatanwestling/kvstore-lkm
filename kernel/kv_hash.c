#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hashtable.h>
#include <linux/string.h> 
#include <linux/list.h>


#include "kv_hash.h"

static inline u32 kv_bucket(const struct hash_table *ht, hash_key_t key)
{
	return hash_min(key, ht->bits);
}

/**
 * kv_hash_init - Initialize a hash table
 * @ht: Pointer to hash_table structure
 * @bits: Number of bits for bucket count (2^bits buckets)
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_init(struct hash_table *ht, u32 bits)
{
  u32 n;

  if (!ht)
  {
     return EINVAL;
  }

  memset(ht, 0, sizeof(*ht));

  ht->bits = bits;
  n = 1U << bits;

  ht->buckets = kmalloc_array(n, sizeof(*ht->buckets), GFP_KERNEL);
  if (!ht->buckets)
  {
      ht->bits = 0;
      return -ENOMEM;
  }
  ht->bucket_locks = kmalloc_array(n, sizeof(spinlock_t), GFP_KERNEL);
  if(!ht->bucket_locks)
  {
    kfree(ht->buckets);
    ht->buckets = NULL;
    ht->bits = 0;
    return -ENOMEM;
  }

  for (int i = 0; i < n; i++)
  {
      INIT_HLIST_HEAD(&ht->buckets[i]);
      spin_lock_init(&ht->bucket_locks[i]);
  }

  return 0;
}

/**
 * kv_hash_destroy - Destroy a hash table and free all associated memory
 * @ht: Pointer to hash_table structure
 */
void kv_hash_destroy(struct hash_table *ht)
{
    u32 bucket;
    u32 n;

    struct kv_hash_entry *e;
	struct hlist_node *tmp;

    if (!ht)
    {
        return;
    }

    n = 1U << ht->bits;

    for (bucket = 0; bucket < n; bucket++)
    {
        hlist_for_each_entry_safe(e, tmp, &ht->buckets[bucket], node)
        {
            hlist_del(&e->node);
            kfree(e->value);
            kfree(e);
        }
    }
    
    kfree(ht->buckets);
    ht->buckets = NULL;
    ht->bits = 0;
}

/**
 * kv_hash_add - Add or update a key-value pair in the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to add or update
 * @value: Value associated with the key
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_add(struct hash_table *ht, hash_key_t key, const char *value)
{
    u32 bucket;
    struct kv_hash_entry *e;
    char *newstr;
    unsigned long flags;

    if (!ht || !ht->buckets || !value)
		return -EINVAL;

    newstr = kstrdup(value, GFP_KERNEL);
    if (!newstr)
    {
        return -ENOMEM;
    }

    bucket = kv_bucket(ht, key);
    spin_lock_irqsave(&ht->bucket_locks[bucket], flags);

    // Update existing key
    hlist_for_each_entry(e, &ht->buckets[bucket], node)
    {
        if (e->key == key)
        {
            kfree(e->value);
            e->value = newstr;
            spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
            return 0;
        }
    }

    // Insert new
    e = kmalloc(sizeof(*e), GFP_KERNEL);
    if (!e)
    {
        kfree(newstr);
        spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
        return -ENOMEM;
    }
    e->key = key;
    e->value = newstr;
    hlist_add_head(&e->node, &ht->buckets[bucket]);

    spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
    return 0;
}

/**
 * kv_hash_remove - Remove a key-value pair from the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to remove
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_remove(struct hash_table *ht, hash_key_t key)
{
    u32 bucket;
    struct kv_hash_entry *e;
    unsigned long flags;

    if (!ht || !ht->buckets)
    {
		return -EINVAL;
    }

    bucket = kv_bucket(ht, key);
    spin_lock_irqsave(&ht->bucket_locks[bucket], flags);
    hlist_for_each_entry(e, &ht->buckets[bucket], node)
    {
        if (e->key == key) 
        {
			hlist_del(&e->node);
			kfree(e->value);
			kfree(e);

            spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
			return 0;
        }
    }
    spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
    return -ENOENT;
}

/**
 * kv_hash_lookup - Lookup a value by key in the hash table
 * @ht: Pointer to hash_table structure
 * @key: Key to lookup
 * @out_buf: Buffer to store the found value
 * @out_buflen: Length of the output buffer
 * Returns number of bytes copied on success, negative error code on failure
 */
ssize_t kv_hash_lookup(struct hash_table *ht, hash_key_t key, char *out_buf, size_t out_buflen)
{
    u32 bucket;
    struct kv_hash_entry *e;
    size_t len;
    unsigned long flags;

    if (!ht || !ht->buckets || !out_buf || out_buflen == 0)
    {
        return -EINVAL;
    }

    bucket = kv_bucket(ht, key);
    spin_lock_irqsave(&ht->bucket_locks[bucket], flags);
    hlist_for_each_entry(e, &ht->buckets[bucket], node)
    {
        if (e->key == key)
        {
            len = strlen(e->value);

            if (len + 1 > out_buflen)
            {
                spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
                return -ENOSPC;
            }
            
            memcpy(out_buf, e->value, len + 1);
            spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
            return (ssize_t)len;
        }
    }
    spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
    return -ENOENT;
}

/**
 * Hash count
*/
int kv_hash_count(struct hash_table *ht){

    u32 bucket;
    u32 n;
    int count = 0;
    unsigned long flags;
    struct kv_hash_entry *e; 

    if (!ht || !ht->buckets)
        return -EINVAL;

    n = 1U << ht->bits;

    for (bucket = 0; bucket < n; bucket++){
        spin_lock_irqsave(&ht->bucket_locks[bucket], flags);

         hlist_for_each_entry(e, &ht->buckets[bucket], node) {
            count++;
        }
        spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
    }

    return count;

}

/**
 * kv_hash_snapshot - Get a snapshot of key-value pairs in the hash table
 * @ht: Pointer to hash_table structure
 * @entries: Output array to store key-value pairs (caller-allocated)
 * @max_entries: Maximum number of entries that can be stored in the output array
 * @out_count: Output parameter to store the actual number of entries returned
 * Returns 0 on success, negative error code on failure
 */
int kv_hash_snapshot(struct hash_table *ht, struct kv_message *entries, u32 max_entries, u32 *out_count)
{
    u32 bucket;
    u32 n;
    u32 count = 0;
    unsigned long flags;
    struct kv_hash_entry *e;

    if (!ht || !ht->buckets || !entries || !out_count || max_entries == 0) {
        return -EINVAL;
    }

    n = 1U << ht->bits;

    for (bucket = 0; bucket < n; bucket++) {
        spin_lock_irqsave(&ht->bucket_locks[bucket], flags);
        // Iterate ovet the entries in the bucket and copy
        hlist_for_each_entry(e, &ht->buckets[bucket], node) {
            // Check that there is enough space in the output array
            if (count >= max_entries) {
                // This should not happen
                spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
                *out_count = count;
                return 0;
            }
            entries[count].status = 0;
            memset(entries[count].value, 0, MAX_VALUE_LEN);
            // Copy key and value
            entries[count].key = (unsigned long long)e->key;
            strscpy(entries[count].value, e->value, MAX_VALUE_LEN);
            count++;
        }

        spin_unlock_irqrestore(&ht->bucket_locks[bucket], flags);
    }
    *out_count = count;
    return 0;
}
