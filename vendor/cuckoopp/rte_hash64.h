/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */


#ifndef RTE_HASH64_H
#define RTE_HASH64_H

#include "x86intrin.h"
#include "immintrin.h"
#include "rte_tchh_structs.h"


/**
 * Hash function based on two CRC32 with different seeds (instead of one in DPDK)
 */
static inline uint64_t dcrc_hash_m128(const hash_key_t k) {
	uint64_t a = k.a;
	uint64_t b = k.b;
    uint64_t crc00 = _mm_crc32_u64(0,a);
    uint64_t crc01 = _mm_crc32_u64(crc00,b);
    uint64_t crc10 = _mm_crc32_u64(0x5bd1e995,b);
    uint64_t crc11 = _mm_crc32_u64(crc10,a);
    uint64_t crc64 = (crc11 << 32)| crc01 ;
    return crc64;
}





/**
 * This is the function that should be called in all places so that using another hash function only
 * requires to change this function.
 */
static inline uint32_t rte_tch_hash_function(const void *key, __rte_unused uint32_t key_len,
				      __rte_unused uint32_t init_val){
	return dcrc_hash_m128(*(const hash_key_t*)key);
}

#endif
