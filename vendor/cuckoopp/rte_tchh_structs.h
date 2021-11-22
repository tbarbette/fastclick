/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#ifndef RTE_TCHH_STRUCTS_H
#define RTE_TCHH_STRUCTS_H

#include "x86intrin.h"
#include "immintrin.h"

struct rte_tch_key {
	union{
		struct {
			uint64_t a;
			uint64_t b;
		};
		__m128i mm;
	};
};

typedef struct rte_tch_key hash_key_t;

struct rte_tch_data {
	union{
		struct {
			uint64_t a;
			uint64_t b;
		};
		__m128i mm;
	};
};

typedef struct rte_tch_data hash_data_t;
typedef uint64_t hash_sig64_t;

#endif
