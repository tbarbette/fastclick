/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#ifndef RTE_HASH_COMMONS
#define RTE_HASH_COMMONS
/**
 * @file
 *
 * RTE Hash Table
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <rte_tchh_structs.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 *  Max exiration period
 */
#define RTE_HASH_HVARIANT_MAX_EXPIRATION_PERIOD   ((uint32_t)1024U)

/** Maximum size of hash table that can be created. */
#define RTE_HASH_HVARIANT_ENTRIES_MAX			(1 << 30)

/** Maximum number of characters in hash name.*/
#define RTE_HASH_HVARIANT_NAMESIZE			32

/** Maximum number of keys that can be searched for using rte_hash_hvariant_lookup_bulk. */
#define RTE_HASH_HVARIANT_LOOKUP_BULK_MAX		64
#define RTE_HASH_HVARIANT_LOOKUP_MULTI_MAX		RTE_HASH_HVARIANT_LOOKUP_BULK_MAX

/** Constants for returning results of operation */
#define RHL_NOT_FOUND -ENOENT
#define RHL_NOT_ADDED -ENOSPC
#define RHL_FOUND_UPDATED 1
#define RHL_FOUND_NOTUPDATED 2

/** Signature of key that is stored internally. */
typedef uint32_t hash_sig32_t;

/**
 * Parameters used when creating the hash table.
 */
struct rte_hash_hvariant_parameters {
	const char *name;		/**< Name of the hash. */
	uint32_t entries;		/**< Total hash table entries. */
	int socket_id;			/**< NUMA Socket ID for memory. */
};

/** @internal A hash table structure. */
struct rte_hash_hvariant;


#ifdef __cplusplus
}
#endif
#endif
