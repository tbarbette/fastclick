/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */


#include <rte_hash_commons.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Create a new hash table.
 *
 * @param params
 *   Parameters used to create and initialise the hash table.
 * @return
 *   Pointer to hash table structure that is used in future hash table
 *   operations, or NULL on error, with error code set in rte_errno.
 *   Possible rte_errno errors include:
 *    - ENOENT - missing entry
 *    - EINVAL - invalid parameter passed to function
 *    - ENOSPC - the maximum number of memzones has already been allocated
 *    - EEXIST - a memzone with the same name already exists
 *    - ENOMEM - no appropriate memory area found in which to create memzone
 */
struct rte_hash_hvariant *
H(rte_hash,create)(const struct rte_hash_hvariant_parameters *params);


/**
 * De-allocate all memory used by hash table.
 * @param h
 *   Hash table to free
 */
void
H(rte_hash,free)(struct rte_hash_hvariant *h);

/**
 * Reset all hash structure, by zeroing all entries
 * @param h
 *   Hash table to reset
 */
void
H(rte_hash,reset)(struct rte_hash_hvariant *h);

/**
 * Print stats to stdout
 * @param h
 *   Hash table to reset
 * @param currentTime
 *   current time to consider
 */
void H(rte_hash,print_stats)(struct rte_hash_hvariant *h, uint16_t currentTime);

/**
 * Get the size of the hash table
 * @param h
 *   Hash table to reset
 * @param currentTime
 *   current time to consider
 */
uint32_t H(rte_hash,size)(struct rte_hash_hvariant *h, uint16_t currentTime);

/**
 * Get the capacity of the hash table
 * @param h
 *   Hash table to reset
 */
uint32_t H(rte_hash,capacity)(struct rte_hash_hvariant *h);

/**
 * Add a key-value pair to an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to add the key to.
 * @param key
 *   Key to add to the hash table.
 * @param data
 *   Data to add to the hash table.
 * @param expirationTime
 *   Timeunit at which is the inserted entry should be expired
 * @param currentTime
 *   Current time unit
 * @return
 *   - RHL_FOUND_UPDATED if the key was added
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOSPC if there is no space in the hash for this key.
 */
int
 H(rte_hash,add_key_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t data, uint16_t expirationTime, uint16_t currentTime);

/**
 * Add a key-value pair with a pre-computed hash value
 * to an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to add the key to.
 * @param key
 *   Key to add to the hash table.
 * @param sig
 *   Precomputed hash value for 'key'
 * @param data
 *   Data to add to the hash table.
 * @param expirationTime
 *   Timeunit at which is the inserted entry should be expired
 * @param currentTime
 *   Current time unit
 * @return
 *   - RHL_FOUND_UPDATED if the key was added
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOSPC if there is no space in the hash for this key.
 */
int32_t
H(rte_hash,add_key_with_hash_data)(struct rte_hash_hvariant *h, const hash_key_t key,
						hash_sig64_t sig, hash_data_t data, uint16_t expirationTime, uint16_t currentTime);

/**
 * Add a key to an existing hash table. This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to add the key to.
 * @param key
 *   Key to add to the hash table.
 * @param expirationTime
 *   Timeunit at which is the inserted entry should be expired
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOSPC if there is no space in the hash for this key.
 *   - RHL_FOUND_UPDATED if the key was added
 */
int32_t
H(rte_hash,add_key)(struct rte_hash_hvariant *h, const hash_key_t key , uint16_t expirationTime, uint16_t currentTime);

/**
 * Add a key to an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to add the key to.
 * @param key
 *   Key to add to the hash table.
 * @param sig
 *   Precomputed hash value for 'key'.
 * @param expirationTime
 *   Timeunit at which is the inserted entry should be expired
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOSPC if there is no space in the hash for this key.
 *   - RHL_FOUND_UPDATED if the key was added
 */
int32_t
H(rte_hash,add_key_with_hash)(struct rte_hash_hvariant *h, const hash_key_t key, hash_sig64_t sig, uint16_t expirationTime,  uint16_t currentTime);

/**
 * Remove a key from an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to remove the key from.
 * @param key
 *   Key to remove from the hash table.
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if the key is not found.
 *   - RHL_FOUND_UPDATED if the key was deleted
 */
int32_t
H(rte_hash,del_key)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t currentTime);

/**
 * Remove a key from an existing hash table.
 * This operation is not multi-thread safe
 * and should only be called from one thread.
 *
 * @param h
 *   Hash table to remove the key from.
 * @param key
 *   Key to remove from the hash table.
 * @param sig
 *   Precomputed hash value for 'key'.
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if the key is not found.
 *   - RHL_FOUND_UPDATED if the key was deleted
 */
int32_t
H(rte_hash,del_key_with_hash)(struct rte_hash_hvariant *h, const hash_key_t key, hash_sig64_t sig, uint16_t currentTime);


/**
 * Find a key-value pair in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param data
 *   Output with pointer to data returned from the hash table.
 * @param currentTime
 *   Current time unit
 * @return
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int
H(rte_hash,lookup_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t *data, uint16_t currentTime);

/**
 * Find a key-value pair with a pre-computed hash value
 * to an existing hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param sig
 *   Precomputed hash value for 'key'
 * @param data
 *   Output with pointer to data returned from the hash table.
 * @param currentTime
 *   Current time unit
 * @return
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int
H(rte_hash,lookup_with_hash_data)(struct rte_hash_hvariant *h, const hash_key_t key,
					hash_sig64_t sig, hash_data_t *data, uint16_t currentTime);

/**
 * Find a key in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if the key is not found.
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 */
int32_t
H(rte_hash,lookup)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t currentTime);

/**
 * Find a key in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param sig
 *   Hash value to remove from the hash table.
 * @param currentTime
 *   Current time unit
 * @return
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if the key is not found.
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 */
int32_t
H(rte_hash,lookup_with_hash)(struct rte_hash_hvariant *h,
				const hash_key_t key, hash_sig64_t sig, uint16_t currentTime);


/**
 * Find a key-value pair in the hash table and update expiration time.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param currentTime
 *   Current time unit
 * @param expieration time
 *   New expiration time
 * @return
 *   - RHL_FOUND_UPDATED if the key was found and expiration time has been updated
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int32_t H(rte_hash,lookup_update)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t expirationTime, uint16_t currentTime);



/**
 * Find a key-value pair in the hash table and update expiration time.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param currentTime
 *   Current time unit
 * @param sig
 *   Hash value to remove from the hash table.
 * @param expiration time
 *   New expiration time
 * @return
 *   - RHL_FOUND_UPDATED if the key was found and expiration time has been updated
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int32_t H(rte_hash,lookup_update_with_hash)(struct rte_hash_hvariant *h, const hash_key_t key, hash_sig64_t sig, uint16_t expirationTime, uint16_t currentTime);

/**
 * Find a key-value pair in the hash table and update expiration time.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param data
 *   Output with pointer to data returned from the hash table.
 * @param currentTime
 *   Current time unit
 * @param sig
 *   Hash value to remove from the hash table.
 * @param expiration time
 *   New expiration time
 * @return
 *   - RHL_FOUND_UPDATED if the key was found and expiration time has been updated
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int H(rte_hash,lookup_update_with_hash_data)(struct rte_hash_hvariant *h,const hash_key_t key, hash_sig64_t sig, hash_data_t *data, uint16_t expirationTime, uint16_t currentTime);

/**
 * Find a key-value pair in the hash table and update expiration time.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param key
 *   Key to find.
 * @param data
 *   Output with pointer to data returned from the hash table.
 * @param currentTime
 *   Current time unit
 * @param expieration time
 *   New expiration time
 * @return
 *   - RHL_FOUND_UPDATED if the key was found and expiration time has been updated
 *   - RHL_FOUND_NOT_UPDATED if the key was found
 *   - EINVAL if the parameters are invalid.
 *   - ENOENT if the key is not found.
 */
int H(rte_hash,lookup_update_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t *data, uint16_t expirationTime, uint16_t currentTime);


/**
 * Find multiple keys in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param keys
 *   A pointer to a list of keys to look for.
 * @param num_keys
 *   How many keys are in the keys list (less than RTE_HASH_LOOKUP_BULK_MAX).
 * @param hit_mask
 *   Output containing a bitmask with all successful lookups.
 * @param data
 *   Output containing array of data returned from all the successful lookups.
 * @param currentTime
 *   Current time unit
 * @return
 *   -EINVAL if there's an error, otherwise number of successful lookups.
 */
int H(rte_hash,lookup_bulk_data)(struct rte_hash_hvariant *h, const hash_key_t *keys, uint32_t num_keys, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime);

/**
 * Find multiple keys in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param keys
 *   A pointer to a list of keys to look for.
 * @param num_keys
 *   How many keys are in the keys list (less than RTE_HASH_LOOKUP_BULK_MAX).
 * @param positions
 *   Output containing a list of values, corresponding to the list of keys that
 *   can be used by the caller as an offset into an array of user data. These
 *   values are unique for each key, and are the same values that were returned
 *   when each key was added. If a key in the list was not found, then -ENOENT
 *   will be the value.
 * @param currentTime
 *   Current time unit
 * @return
 *   -EINVAL if there's an error, otherwise 0.
 */
int H(rte_hash,lookup_bulk)(struct rte_hash_hvariant *h, const hash_key_t *keys, uint32_t num_keys, int32_t *positions, uint16_t currentTime);

/**
 * Find multiple keys in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param keys
 *   A pointer to a list of keys to look for (up to 64 keys, according to lookup_mask).
 * @param lookup_mask
 *  bitmask of keys to lookup
 * @param hit_mask
 *   Output containing a bitmask with all successful lookups.
 * @param data
 *   Output containing array of data returned from all the successful lookups.
 * @param currentTime
 *   Current time unit
 * @return
 *   -EINVAL if there's an error, otherwise number of successful lookups.
 */
int H(rte_hash,lookup_bulk_data_mask)(struct rte_hash_hvariant *h, const hash_key_t *keys, uint64_t lookup_mask, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime);

/**
 * Find multiple keys in the hash table.
 * This operation is multi-thread safe.
 *
 * @param h
 *   Hash table to look in.
 * @param keys
 *   A pointer to a list of keys to look for (up to 64 keys, according to lookup_mask).
 * @param lookup_mask
 *  bitmask of keys to lookup
 * @param hit_mask
 *   Output containing a bitmask with all successful lookups.
 * @param updated_mask
 *   Output containing a bitmask with all updates expiration times.
 * @param data
 *   Output containing array of data returned from all the successful lookups.
 * @param currentTime
 *   Current time unit
 * @param expirationTime 
 *   A pointer to a list of expiration times for each keys (up to 64 expiration times, according to lookup_mask).
 * @return
 *   -EINVAL if there's an error, otherwise number of successful lookups.
 */
int H(rte_hash,lookup_update_bulk_data_mask)(struct rte_hash_hvariant *h, const hash_key_t *keys, uint64_t lookup_mask, uint64_t *hit_mask, uint64_t *updated_mask, hash_data_t data[], uint16_t * newExpirationTime, uint16_t currentTime);


/**
 * Reset iterator (only a single iterator can be active at a time on the hashtable)
 *
 *  @param h
 *   Hash table to iterate
 */
void H(rte_hash,iterator_reset)(struct rte_hash_hvariant *h);

/**
 * Iterate through the hash table, returning key-value pairs.
 * This version supports modifications to the hash table in between calls to iterate.
 * Reset iterator must be called before starting iteration on the hash table.
 *
 * @param h
 *   Hash table to iterate
 * @param key
 *   Output containing the key where current iterator was pointing at
 * @param data
 *   Output containing the data associated with key.
 *   Returns NULL if data was not stored.
 * @param currentTime
 *   Current time unit
 * @param remainingTime
 *   Output containing remaining time before expiration (relative to currentTime)
 * @return
 *   - 0 if a key/data is returned
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if end of the hash table.
 *   - -EBUSY if the iteration was suspended (and should be continued by a new call). This allows to limit the duration of unpreemptable calls to this function.
 */
int32_t
H(rte_hash,iterate)(struct rte_hash_hvariant *h, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime);

/**
 * Iterate through the hash table, returning key-value pairs. Unsafe version, multiple iterators allowed, but entries may be missed if modification are concurrent.
 *
 * @param h
 *   Hash table to iterate
 * @param pos
 * 	 Placeholder for iterator data (must be set to 0 for starting an iteration)
 * @param key
 *   Output containing the key where current iterator
 *   was pointing at
 * @param data
 *   Output containing the data associated with key.
 *   Returns NULL if data was not stored.
 * @param currentTime
 *   Current time unit
 * @param remainingTime
 *   Output containing remaining time before expiration (relative to currentTime)
 * @return
 *   - 0 if a key/data is returned
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if end of the hash table.
 */
int32_t
H(rte_hash,unsafe_iterate)(struct rte_hash_hvariant *h, uint64_t * pos, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime);

/**
 * Check the integrity of the structure. This function is meant to be used during development or testing.
 *
 * @param h
 *   Hash table to iterate
 * @param currentTime
 *   Current time unit
 * @return 
 *   Nothing. Errors are printed to standard output. 
 */
void H(rte_hash,check_integrity)(struct rte_hash_hvariant *h, uint16_t currentTime);

/**
 * Check the integrity of the structure
 *
 * @param h
 *   Hash table to iterate
 * @param currentTime
 *   Current time unit
 * @return 
 * 	 Stasticis on fraction of entries stored in secondary bucket.
 */
double H(rte_hash,stats_secondary)(struct rte_hash_hvariant *h, uint16_t currentTime);

/**
 * Returns the number of slots per bucket
 *
 * @return 
 * 	 the number of slots per bucket
 */
int H(rte_hash,slots_per_bucket)(void);


#ifdef __cplusplus
}
#endif
