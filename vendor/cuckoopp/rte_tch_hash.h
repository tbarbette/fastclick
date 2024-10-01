/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#ifndef LIBRTE_TCH_HASH_RTE_TCH_HASH_H_
#define LIBRTE_TCH_HASH_RTE_TCH_HASH_H_

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include <rte_common.h>
#include <rte_tchh_structs.h>

#include <rte_hash_horton.h>
#include <rte_hash_bloom.h>
#include <rte_hash_uncond.h>
#include <rte_hash_cond.h>
#include <rte_hash_lazy_bloom.h>
#include <rte_hash_lazy_cond.h>
#include <rte_hash_lazy_uncond.h>
#include <rte_hash_lazy_no.h>
#include <rte_hash_v1604.h>
#include <rte_hash_v1702.h>
#include <math.h>

#include <rte_tchh_structs.h>
#include <rte_malloc.h>

enum rte_tch_hash_variants {
	H_V1604 = 0,
	H_V1702,
	H_LAZY_BLOOM,
	H_LAZY_COND,
	H_LAZY_UNCOND,
	H_LAZY_NO,
	H_HORTON,
	H_BLOOM,
	H_COND,
	H_UNCOND
};

const char * variants_names[] = {
		"DPDK_1604",
		"DPDK_1702",
		"LAZY_BLOOM",
		"LAZY_COND",
		"LAZY_UNCOND",
		"LAZY_NO",
		"HORTON",
		"BLOOM",
		"COND",
		"UNCOND"
};

/** @internal A hash table structure. */
struct rte_tch_hash {
	union {
		struct rte_hash_v1604 * h_dpdk1604;
		struct rte_hash_v1702 * h_dpdk1702;
		struct rte_hash_hvariant * h_tch; // Non-defined type (polymorphic)
	};
};

/**
 * Parameters used when creating the hash table.
 */
struct rte_tch_hash_parameters {
	uint32_t entries;		/**< Total hash table entries. */
	int socket_id;			/**< NUMA Socket ID for memory. */
};


#define EXPAND(F) \
	if(v == H_HORTON){ F(horton)} \
	if(v == H_LAZY_BLOOM){ F(lazy_bloom)} \
	if(v == H_BLOOM){ F(bloom)} \
	if(v == H_LAZY_COND){ F(lazy_cond)} \
	if(v == H_COND){ F(cond)} \
	if(v == H_LAZY_UNCOND){ F(lazy_uncond)} \
	if(v == H_UNCOND){ F(uncond)} \
	if(v == H_LAZY_NO){ F(lazy_no)}




/**
 * Reset all hash structure, by zeroing all entries
 * @param h
 *   Hash table to reset
 */
#define RESET(x) return rte_hash_##x##_reset(h->h_tch);
static inline void rte_tch_hash_reset(enum rte_tch_hash_variants v,struct rte_tch_hash *h){
	if(v == H_V1604) return rte_hash_v1604_reset(h->h_dpdk1604);
	if(v == H_V1702) return rte_hash_v1702_reset(h->h_dpdk1702);
	EXPAND(RESET)
}

/**
 * Print stats to stdout
 */
#define PRINT(x) return rte_hash_##x##_print_stats(h->h_tch,currentTime);
static inline void rte_tch_hash_print_stats(enum rte_tch_hash_variants v,struct rte_tch_hash *h, uint16_t currentTime){
	if(v == H_V1604) return ;
	if(v == H_V1702) return ;
	EXPAND(PRINT)
}

/**
 * Create a new hash table.
 *
 * @param params
 *   Parameters used to create and initialise the hash table.
 * @return
 *   Pointer to hash table structure that is used in future hash table
 *   operations, or NULL on error, with error code set in rte_errno.
 *   Possible rte_errno errors include:
 *    - E_RTE_NO_CONFIG - function could not get pointer to rte_config structure
 *    - E_RTE_SECONDARY - function was called from a secondary process instance
 *    - ENOENT - missing entry
 *    - EINVAL - invalid parameter passed to function
 *    - ENOSPC - the maximum number of memzones has already been allocated
 *    - EEXIST - a memzone with the same name already exists
 *    - ENOMEM - no appropriate memory area found in which to create memzone
 */
#define CREATE(x) 	h->h_tch = rte_hash_##x##_create(&p);
static inline struct rte_tch_hash *
rte_tch_hash_create(enum rte_tch_hash_variants v, const struct rte_tch_hash_parameters *params){
	char buf[L_tmpnam];
	char * name = tmpnam(buf);
	struct rte_tch_hash  * h = (struct rte_tch_hash *) rte_zmalloc(NULL,sizeof(struct rte_tch_hash),64);
	struct rte_hash_hvariant_parameters p;
	p.entries=params->entries;
	p.socket_id=params->socket_id;
	p.name=name;

	if(name == NULL) rte_exit(EXIT_FAILURE, "Failed to generate temporary name for hash table\n");
	if(v == H_V1604){
		struct rte_hash_v1604_parameters p;
		p.entries=params->entries;
		p.key_len=16;
		p.socket_id=params->socket_id;
		p.name=name;
		p.extra_flag = 0;
		p.reserved = 0;
		p.hash_func_init_val = 0xffeeffee;
		p.hash_func = rte_tch_hash_function;
		h->h_dpdk1604 = rte_hash_v1604_create(&p);
	}
	if(v == H_V1702){
		struct rte_hash_v1702_parameters p;
		p.entries=params->entries;
		p.key_len=16;
		p.socket_id=params->socket_id;
		p.name=name;
		p.extra_flag = 0;
		p.reserved = 0;
		p.hash_func_init_val = 0xffeeffee;
		p.hash_func = rte_tch_hash_function;
		h->h_dpdk1702 = rte_hash_v1702_create(&p);
	}
	EXPAND(CREATE)
	return h;
}



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
 *   - -ENOSPC if there is>= 0 ? RHL_FOUND_NOTUPDATED : -1 no space in the hash for this key.
 */
#define ADDKD(x) return rte_hash_##x##_add_key_data(h->h_tch,key,data,expirationTime,currentTime);
static inline int rte_tch_hash_add_key_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key, hash_data_t data, uint16_t expirationTime, uint16_t currentTime){
	if(v == H_V1604) return rte_hash_v1604_add_key_data(h->h_dpdk1604,&key,data) >= 0 ? RHL_FOUND_UPDATED : -1  ;
	if(v == H_V1702) return rte_hash_v1702_add_key_data(h->h_dpdk1702,&key,data) >= 0 ? RHL_FOUND_UPDATED : -1  ;
	EXPAND(ADDKD);
	return -1;
}

#define DELK(x) return rte_hash_##x##_del_key(h->h_tch,key,currentTime);
static inline int rte_tch_hash_del_key(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key, uint16_t currentTime){
	if(v == H_V1604) return rte_hash_v1604_del_key(h->h_dpdk1604,&key) >= 0 ? RHL_FOUND_UPDATED : -1  ;
	if(v == H_V1702)return rte_hash_v1702_del_key(h->h_dpdk1702,&key) >= 0 ? RHL_FOUND_UPDATED : -1  ;
	EXPAND(DELK);
	return -1;
}


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
#define LOOKD(x) return rte_hash_##x##_lookup_data(h->h_tch,key,data,currentTime);
static inline int rte_tch_hash_lookup_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key, hash_data_t *data, uint16_t currentTime){
	if(v == H_V1604)return rte_hash_v1604_lookup_data(h->h_dpdk1604,&key,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1  ;
	if(v == H_V1702) return rte_hash_v1702_lookup_data(h->h_dpdk1702,&key,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1  ;
	EXPAND(LOOKD);
	return -1;
}

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
#define LOOKDH(x) 		return rte_hash_##x##_lookup_with_hash_data(h->h_tch,key,sig,data,currentTime);
static inline int rte_tch_hash_lookup_with_hash_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key,
					hash_sig64_t sig, hash_data_t *data, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_lookup_with_hash_data(h->h_dpdk1604,&key,sig,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1  ;
	}
	if(v == H_V1702){
		return rte_hash_v1702_lookup_with_hash_data(h->h_dpdk1702,&key,sig,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	EXPAND(LOOKDH)
	return -1;
}

#define LOOKH(x) 		return rte_hash_##x##_lookup_with_hash(h->h_tch,key,sig,currentTime);
static inline int rte_tch_hash_lookup_with_hash(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key,
					hash_sig64_t sig, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_lookup_with_hash(h->h_dpdk1604,&key,sig) >= 0 ? RHL_FOUND_NOTUPDATED : -1  ;
	}
	if(v == H_V1702){
		return rte_hash_v1702_lookup_with_hash(h->h_dpdk1702,&key,sig) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	EXPAND(LOOKH)
	return -1;
}


#define LOOKDUH(x) 		return rte_hash_##x##_lookup_update_with_hash_data(h->h_tch,key,sig,data,expirationTime,currentTime);
static inline int rte_tch_hash_lookup_update_with_hash_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h,const hash_key_t key, hash_sig64_t sig, hash_data_t *data, uint16_t expirationTime, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_lookup_with_hash_data(h->h_dpdk1604,&key,sig,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	if(v == H_V1702){
		return rte_hash_v1702_lookup_with_hash_data(h->h_dpdk1702,&key,sig,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	EXPAND(LOOKDUH)
	return -1;
}

#define LOOKDU(x) 		return rte_hash_##x##_lookup_update_data(h->h_tch,key,data,expirationTime,currentTime);
static inline int rte_tch_hash_lookup_update_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key, hash_data_t *data, uint16_t expirationTime, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_lookup_data(h->h_dpdk1604,&key,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	if(v == H_V1702){
		return rte_hash_v1702_lookup_data(h->h_dpdk1702,&key,data) >= 0 ? RHL_FOUND_NOTUPDATED : -1;
	}
	EXPAND(LOOKDU)
	return -1;
}


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
#define ADDKHD(x) 		return rte_hash_##x##_add_key_with_hash_data(h->h_tch,key,sig,data,expirationTime,currentTime);

static inline int32_t
rte_tch_hash_add_key_with_hash_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t key,
						hash_sig64_t sig, hash_data_t data, uint16_t expirationTime, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_add_key_with_hash_data(h->h_dpdk1604,&key,sig,data) >= 0 ? RHL_FOUND_UPDATED: -ENOSPC ;
	}
	if(v == H_V1702){
		return rte_hash_v1702_add_key_with_hash_data(h->h_dpdk1702,&key,sig,data) >= 0 ? RHL_FOUND_UPDATED: -ENOSPC;
	}
	EXPAND(ADDKHD)
	return -1;
}

static inline void keys_to_ptr(const void ** keys_ptr, const hash_key_t * keys, uint32_t num_keys){
	assert(num_keys <= 64);
	uint32_t i;
	for(i=0;i<num_keys;i++){
		keys_ptr[i] = &keys[i];
	}
}

#define BULKD(x) 		return rte_hash_##x##_lookup_bulk_data(h->h_tch,keys, num_keys,hit_mask,data,currentTime);
static inline int rte_tch_hash_lookup_bulk_data(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t *keys, uint32_t num_keys, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime){
	if(v == H_V1604){
		const void * keys_ptr[64];
		keys_to_ptr(keys_ptr,keys,num_keys);
		return rte_hash_v1604_lookup_bulk_data(h->h_dpdk1604,keys_ptr, num_keys,hit_mask,data);
	}
	if(v == H_V1702){
		const void * keys_ptr[64];
		keys_to_ptr(keys_ptr,keys,num_keys);
		return rte_hash_v1702_lookup_bulk_data(h->h_dpdk1702,keys_ptr, num_keys,hit_mask,data);
	}
	EXPAND(BULKD)
	return -1;
}

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
//static inline int rte_tch_hash_lookup_bulk(enum rte_tch_hash_variants v, struct rte_hash_lazy *h, const hash_key_t *keys, uint32_t num_keys, int32_t *positions, uint16_t currentTime);

#define FOREACH_IN_MASK64(i,m,mn) for(mn=m,i=__builtin_ctzl(mn); mn != 0; mn &= ~(1llu << i),i=__builtin_ctzl(mn))

#define BULKDM(x) return rte_hash_##x##_lookup_bulk_data_mask(h->h_tch,keys, lookup_mask,hit_mask,data,currentTime);
static inline int rte_tch_hash_lookup_bulk_data_mask(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t *keys, uint64_t lookup_mask, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime){
	if(v == H_V1604){
		return -1;
	}else if(v == H_V1702){
		int i;
		int j=0;
		uint64_t tmp_mask;
		(*hit_mask) =0;
		const void * keys_compact[64];
		hash_data_t data_compact[64];
		uint64_t hit_mask_compact=0;
		FOREACH_IN_MASK64(i,lookup_mask,tmp_mask){
			keys_compact[j] = &keys[i];
			j++;
		}
		int r=rte_hash_v1702_lookup_bulk_data(h->h_dpdk1702,keys_compact,j,&hit_mask_compact,data_compact);
		FOREACH_IN_MASK64(i,lookup_mask,tmp_mask){
			int match = (hit_mask_compact >> j) & 1;
			if(match){
				data[i] = data_compact[j];
				(*hit_mask) |= (1 << i);
			}
			j++;
		}
		return r;
	}
	EXPAND(BULKDM)
	return -1;
}

#define BULKDUM(x) return rte_hash_##x##_lookup_update_bulk_data_mask(h->h_tch,keys, lookup_mask,hit_mask,updated_mask,data,newExpirationTime,currentTime);
static inline int rte_tch_hash_lookup_update_bulk_data_mask(enum rte_tch_hash_variants v, struct rte_tch_hash *h, const hash_key_t *keys, uint64_t lookup_mask, uint64_t *hit_mask, uint64_t *updated_mask, hash_data_t data[], uint16_t * newExpirationTime, uint16_t currentTime){
	if(v == H_V1604){
		int r = rte_tch_hash_lookup_bulk_data_mask(v,h,keys,lookup_mask,hit_mask,data,currentTime);
		*updated_mask=*hit_mask;
		return r;
	}
	if(v == H_V1702){
		int r =  rte_tch_hash_lookup_bulk_data_mask(v,h,keys,lookup_mask,hit_mask,data,currentTime);
		*updated_mask=*hit_mask;
		return r;
	}
	EXPAND(BULKDUM)
	return -1;
}


/**
 * Reset iterator (only a single iterator can be active at a time on the hashtable)
 *
 *  @param h
 *   Hash table to iterate
 */
#define RESET_ITERATOR(x) return rte_hash_##x##_iterator_reset(h->h_tch);
static inline void rte_tch_hash_iterator_reset(__rte_unused enum rte_tch_hash_variants v, __rte_unused struct rte_tch_hash *h){
	if(v == H_V1604){
		assert(0);
	}
	if(v == H_V1702){
		assert(0);
	}
	EXPAND(RESET_ITERATOR);
	return;

}


/**
 * Iterate through the hash table, returning key-value pairs.
 *
 * @param h
 *   Hash table to iterate
 * @param key
 *   Output containing the key where current iterator
 *   was pointing at
 * @param data
 *   Output containing the data associated with key.
 *   Returns NULL if data was not stored.
 * @param currentTime
 *   Current time unit
 * @return
 *   Position where key was stored, if successful.
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if end of the hash table.
 */
#define ITERATE(x) return rte_hash_##x##_iterate(h->h_tch,key,data,remaining_time,currentTime);
static inline int32_t
rte_tch_hash_iterate(enum rte_tch_hash_variants v, struct rte_tch_hash *h, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime){
	if(v == H_V1604){
		key->a = 0;
		key->b = 0;
		data->a = 0;
		data->b = 0;
	}
	if(v == H_V1702){
		key->a = 0;
		key->b = 0;
		data->a = 0;
		data->b = 0;
	}
	EXPAND(ITERATE)
	return -1;
}

/**
 * Iterate through the hash table, returning key-value pairs -- unsafe version (for debugging/statistics only!!!).
 * Multiple unsafe iterator can be run on the hashtable.
 *
 * @param h
 *   Hash table to iterate
 * @param pos
 *   Position in the hash table
 * @param key
 *   Output containing the key where current iterator
 *   was pointing at
 * @param data
 *   Output containing the data associated with key.
 *   Returns NULL if data was not stored.
 * @param currentTime
 *   Current time unit
 * @return
 *   Position where key was stored, if successful.
 *   - -EINVAL if the parameters are invalid.
 *   - -ENOENT if end of the hash table.
 */
#define ITERATEUN(x) return rte_hash_##x##_unsafe_iterate(h->h_tch,pos,key,data,remaining_time,currentTime);
static inline int32_t
rte_tch_hash_unsafe_iterate(enum rte_tch_hash_variants v, struct rte_tch_hash *h, uint64_t * pos, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime){
	if(v == H_V1604){
		key->a = 0;
		key->b = 0;
		data->a = 0;
		data->b = 0;
	}
	if(v == H_V1702){
		key->a = 0;
		key->b = 0;
		data->a = 0;
		data->b = 0;
	}
	EXPAND(ITERATEUN)
	return -1;
}

/**
 * Check the integrity of the structure
 *
 * @param h
 *   Hash table to iterate
 * @param currentTime
 *   Current time unit
 */
#define INTEGRITY(x) rte_hash_##x##_check_integrity(h->h_tch,currentTime);
static inline void rte_tch_hash_check_integrity(enum rte_tch_hash_variants v, struct rte_tch_hash *h, uint16_t currentTime){
	if(v == H_V1604){
		return ;
	}else if(v == H_V1702){
		return ;
	}
	EXPAND(INTEGRITY)
}

#define FREE(x) rte_hash_##x##_free(h->h_tch);
static inline void rte_tch_hash_free(enum rte_tch_hash_variants v, struct rte_tch_hash *h){
	if(v == H_V1604){
		return rte_hash_v1604_free(h->h_dpdk1604);
	}else if(v == H_V1702){
		return rte_hash_v1702_free(h->h_dpdk1702);
	}
	EXPAND(FREE)
	rte_free(h);
}

#define SIZE(x) return rte_hash_##x##_size(h->h_tch,currentTime);
static inline int rte_tch_hash_size(enum rte_tch_hash_variants v, struct rte_tch_hash *h, uint16_t currentTime){
	if(v == H_V1604){
		return -1;
	}else if(v == H_V1702){
		return -1;
	}
	EXPAND(SIZE)
	return -1;
}

#define CAPACITY(x) return rte_hash_##x##_capacity(h->h_tch);
static inline uint32_t rte_tch_hash_capacity(enum rte_tch_hash_variants v, struct rte_tch_hash *h){
	if(v == H_V1604){
		return -1;
	}else if(v == H_V1702){
		return -1;
	}
	EXPAND(CAPACITY)
	return -1;
}


#define STATS(x) return rte_hash_##x##_stats_secondary(h->h_tch, currentTime);
static inline double rte_tch_hash_stats_secondary(enum rte_tch_hash_variants v, struct rte_tch_hash *h, uint16_t currentTime){
	if(v == H_V1604){
		return rte_hash_v1604_stats_secondary(h->h_dpdk1604);
	}else if(v == H_V1702){
		return nan("");
	}
	EXPAND(STATS)
	return nan("");
}

#define BUCKETS(x) return rte_hash_##x##_slots_per_bucket();
static inline int rte_tch_hash_slots_per_bucket(enum rte_tch_hash_variants v){
	if(v == H_V1604){
		return 4;
	}else if(v == H_V1702){
		return 8;
	}
	EXPAND(BUCKETS)
	return 0;
}

static inline const char * rte_tch_hash_str(enum rte_tch_hash_variants v){
	if(v < sizeof(variants_names)){
		return variants_names[v];
	}else{
		return "UNKNOWN";
	}
}

#endif /* LIBRTE_TCH_HASH_RTE_TCH_HASH_H_ */
