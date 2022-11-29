/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#ifndef _RTE_TCH_UTILS_H_
#define _RTE_TCH_UTILS_H_
#include <stdlib.h>
#include <rte_random.h>

struct rte_tch_rand_state{
	uint64_t s0;
	uint64_t s1;
};

/* The state must be seeded so that it is not everywhere zero. */
static inline uint64_t xorshift128plus(struct rte_tch_rand_state *r) {
	uint64_t x = r->s0;
	uint64_t const y = r->s1;
	r->s0 = y;
	x ^= x << 23; // a
	r->s1 = x ^ y ^ (x >> 17) ^ (y >> 26); // b, c
	return r->s1 + y;
}



static inline uint32_t random_max(uint32_t max, struct rte_tch_rand_state *r){
	uint64_t rd = xorshift128plus(r) & 0xffffffffULL;
	uint64_t res = ( rd * (uint64_t)max ) >> 32ULL; // This is more or less the same as a modulo max, but much faster to execute.
	return res;
}


static inline void rte_tch_rand_init(struct rte_tch_rand_state *r){
	r->s0 = rte_rand();
	r->s1 = rte_rand();
}
/**
 * Force alignment to TWO cache lines to avoid side effects of the adjacent cache line prefetcher.
 */
#define __rte_tch_twocache_aligned __rte_aligned(2*RTE_CACHE_LINE_SIZE)

#endif
