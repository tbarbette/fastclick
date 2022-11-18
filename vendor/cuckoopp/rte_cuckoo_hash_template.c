/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */



#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_memory.h>         /* for definition of RTE_CACHE_LINE_SIZE */
#include <rte_log.h>
#include <rte_memcpy.h>
#include <rte_prefetch.h>
#include <rte_branch_prediction.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_per_lcore.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_cpuflags.h>
#include <rte_log.h>
#include <rte_rwlock.h>
#include <rte_spinlock.h>
#include <rte_ring.h>
#include <rte_compat.h>
#include <assert.h>
#include <math.h>

#include <x86intrin.h>
//#include "rte_hash_template.h"

#include "rte_hash64.h"


/* Macro to enable/disable run-time checking of function parameters */
#if defined(RTE_LIBRTE_HASH_LAZY_DEBUG)
#define RETURN_IF_TRUE(cond, retval) do { \
	if (cond) \
		return retval; \
} while (0)
#else
#define RETURN_IF_TRUE(cond, retval)
#endif


/* Number of items per bucket.
 *  -- This can be set to 1,2,4 or 8
 *  -- 8 is the default value, other values should not be used appart for specific experiments
 */
#define RTE_HASH_HVARIANT_BUCKET_ENTRIES		8U

#define NULL_SIGNATURE			0ULL

#define KEY_ALIGNMENT			16

#define LCORE_CACHE_SIZE		8

/** Max number of recursion in key displacement between buckets */
#define RTE_HASH_HVARIANT_MAX_RECURSION 512

#define ENTRIES_MASK ((1U << RTE_HASH_HVARIANT_BUCKET_ENTRIES)-1U)

#define FAST_ITERATOR
#define FAST_RESET_ITERATOR

#define ITERATOR_GROUP 32


/**
 * Constants usefuls for bulk lookup. Initialized at the hash table initialization
 */
static __m128i max_expiration_time;

/**
 *  Modulo 32768
 */
#define timeunit_modulo(x)  (x & 0xffff)
/**
 *  Check for expired timer. In C there is not computation on 16 bit types so everything is promoted to 32bits.
 *  http://stackoverflow.com/questions/10047956/c-uint16-t-subtraction-behavior-in-gcc
 *  We need to account for this by adding a mask.
 */
#define time_diff(a,b) ((((uint32_t)a-(uint32_t)b)&0xffffU))
#define expired_timer(timer, current) (time_diff(timer,current) >= RTE_HASH_HVARIANT_MAX_EXPIRATION_PERIOD)


/** A hash table structure. */
struct rte_hash_hvariant {
	uint32_t entries;               /**< Total table entries. */
	uint32_t num_buckets;           /**< Number of buckets in table. */
	uint32_t bucket_bitmask;        /**< Bitmask for getting bucket index from hash signature. */
	uint32_t iter_bucket_idx;          /* Next bucket index to start iterating from. */
	uint64_t *iter_group_bucket_mask; /* Bit mask of group iterated buckets => 1 bit = ITER_GROUP buckets */
	uint64_t *reset_group_bucket_mask; /* Bit mask of group to_reset buckets => 1 bit = ITER_GROUP */

	struct rte_hash_hvariant_key *key_store;                /**< Table storing all keys and data */
	struct rte_hash_hvariant_bucket *buckets;	/**< Table with buckets storing all the
												 hash values and key indexes
												 to the key table*/

	char name[RTE_HASH_HVARIANT_NAMESIZE];   /**< Name of the hash. */
} __rte_cache_aligned;


/* Structure that stores key-value pair */
struct rte_hash_hvariant_key {
	hash_key_t key;  // 128 bits
	hash_data_t data; // 128 bits
} __attribute__((aligned(KEY_ALIGNMENT)));


#define FREE_ENTRY 0x8000

static inline void set_bit_in_largemask(uint64_t* large_mask, int il){
	int is = il >> 6; // /64
	int i = il & 0x3f; // %64
	large_mask[is] |= (1ULL << i);
}

static inline void unset_bit_in_largemask(uint64_t * large_mask, int il){
	int is = il >> 6; // /64
	int i = il & 0x3f;// %64
	large_mask[is] &= ~(1ULL << i);
}

static inline uint64_t get_bit_in_largemask(const uint64_t * large_mask, int il){
	int is = il >> 6; // /64
	int i = il & 0x3f;// %64
	return large_mask[is] & (1ULL << i);
}

//static inline void set_bit_in_mask64(uint64_t * mask, int i){
//	*mask |= (1ULL << i);
//}
//
//static inline void unset_bit_in_mask64(uint64_t * mask, int i){
//	*mask &= ~(1ULL << i);
//}
//
//static inline uint64_t get_bit_in_mask64(const uint64_t * mask, int i){
//	return *mask & (1ULL << i);
//}

static inline void set_bit_in_mask(uint8_t * mask, int i){
	*mask |= (1 << i);
}

static inline void unset_bit_in_mask(uint8_t * mask, int i){
	*mask &= ~(1 << i);
}

static inline uint8_t get_bit_in_mask(const uint8_t * mask, int i){
	return *mask & (1 << i);
}

static inline void copy_bit_in_mask(uint8_t * mask_from, uint8_t * mask_to, int ifrom, int ito){
	uint8_t v = get_bit_in_mask(mask_from,ifrom) << ito;
	*mask_to = ((*mask_to) & ~(1 <<ito)) | v;
}

__extension__
typedef void    *MARKER[0];   /**< generic marker for a point in a structure */


/** Bucket structure */
struct rte_hash_hvariant_bucket {
//	MARKER cacheline0 __rte_cache_min_aligned;

	// Aligned entries for SIMD loading
	uint16_t primary_signature_high[8]; // 128 bits
#if TIMER
	uint16_t expire_date_timeunit[8]; // 128 bits
#endif

#if BLOOM
	 uint64_t bloom_moved; // 64 bits
	 uint32_t count_moved_to_secondary;// 32 bits // Count the number of moved (if 0 we can reset the bloom_moved filter)
#endif

#if HORTON
	 uint64_t remap_array_21x3; // Remap array: 21 entries of 3 bits each
#endif

	 // Masks: 32 bits total
	 uint8_t mask_busy; // 0 if entry is free, 1 if entry is busy (you still have to look at expire_date to know if entry is expired).
	 uint8_t mask_in_secondary_position; // 1 if an entry is in secondary position, 0 otherwise, used to updated count_moved and bloom filter
	 uint8_t mask_iterated_over; // 1 if an entry as already been iterated over, 0 otherwise
	 uint8_t mask_already_considered_for_swap; // 1 if entry is already on a the cuckoo path, 0 otherwise

	 uint32_t secondary_signature_full[8]; // 256 bits -- Half of it is in secondary cacheline
	// The bloom filter for checking if a given key might have been moved - Derive 4 6-bit hashes directly from 32 bit secondary key. Insert them in bloom filter
	//     False positive rate with this setting when 8 values are inserted: (1.0-e(-4*8.0/64.0))^(4) = 0.02396
	//     False positive rate with this setting when 4 values are inserted: (1.0-e(-4*8.0/64.0))^(4) = 0.00239
	//     False positive rate with 32-bit bloom filter and  setting when 8 values are inserted: (1.0-e(-4*8.0/32.0))^(4) = 0.16
	//     False positive rate with 32-bit bloom filter and  setting when 4 values are inserted: (1.0-e(-4*8.0/32.0))^(4) = 0.023


} __rte_cache_aligned;


/* New control structure for iterating over masks */
#define FOREACH_IN_MASK32(i,m,mn) for(mn=m,i=__builtin_ctz(mn); mn != 0; mn &= ~(1lu << i),i=__builtin_ctz(mn))
#define FOREACH_IN_MASK64(i,m,mn) for(mn=m,i=__builtin_ctzl(mn); mn != 0; mn &= ~(1llu << i),i=__builtin_ctzl(mn))

/* Example usage
  FOREACH_IN_MASK(idx, mask_to_iterate, tmpmask1){
	array[idx]=1;
}*/


static inline void update_iter_idx(struct rte_hash_hvariant *h, struct rte_hash_hvariant_bucket * b){
	uint32_t b_idx = b - h->buckets;
	h->iter_bucket_idx = RTE_MIN(h->iter_bucket_idx, b_idx);
	unset_bit_in_largemask(h->iter_group_bucket_mask, b_idx/ITERATOR_GROUP); // Group of 8 buckets (16M
}

static inline void reset_iterator_group(struct rte_hash_hvariant *h, uint32_t group){
	unset_bit_in_largemask(h->reset_group_bucket_mask, group);

#ifdef FAST_RESET_ITERATOR
	for(uint32_t i=group*ITERATOR_GROUP;i<(group+1)*ITERATOR_GROUP;i++){
			h->buckets[i].mask_iterated_over = 0;
	}
#endif
}

#if TIMER
static inline int free_or_expired(const struct rte_hash_hvariant_bucket* b, int i, uint16_t currentTime){
	int expired=expired_timer(b->expire_date_timeunit[i],currentTime);
	return (!get_bit_in_mask(&b->mask_busy,i))|| expired;
}

static inline int matches_and_not_expired(const struct rte_hash_hvariant_bucket* b, int i, uint32_t hash, uint16_t currentTime){
	int expired=expired_timer(b->expire_date_timeunit[i],currentTime);
	return (b->primary_signature_high[i] == (hash >> 16)) && get_bit_in_mask(&b->mask_busy,i) && !expired;
}

static inline uint32_t matches_and_not_expired_maskpos(const struct rte_hash_hvariant_bucket* b, uint32_t hash, uint16_t currentTime){
	__m128i current_time_simd = _mm_set1_epi16(currentTime);
	__m128i ref_hashes = _mm_set1_epi16(hash >> 16);

	__m128i bucket_hashes = _mm_load_si128((__m128i*)&b->primary_signature_high);
	__m128i bucket_expiration = _mm_load_si128((__m128i*)&b->expire_date_timeunit);

	__m128i eq_hash = _mm_cmpeq_epi16(bucket_hashes,ref_hashes);
	__m128i diff_time = _mm_sub_epi16(bucket_expiration,current_time_simd);
	__m128i non_expired = _mm_cmpgt_epi16(max_expiration_time,diff_time);

	__m128i matches = _mm_and_si128(eq_hash,non_expired);
	__m128i matches_8bit = _mm_packs_epi16(matches, matches);

	return _mm_movemask_epi8(matches_8bit) & b->mask_busy;
}

static inline uint32_t free_or_expired_maskpos(const struct rte_hash_hvariant_bucket* b, uint16_t currentTime){
	__m128i current_time_simd = _mm_set1_epi16(currentTime);
	__m128i bucket_expiration = _mm_load_si128((__m128i*)&b->expire_date_timeunit);

	__m128i diff_time = _mm_sub_epi16(bucket_expiration,current_time_simd);
	__m128i expired_lt = _mm_cmplt_epi16(max_expiration_time,diff_time);
	__m128i expired_eq = _mm_cmpeq_epi16(max_expiration_time,diff_time);
	__m128i expired = _mm_or_si128(expired_lt,expired_eq);
	__m128i expired_8bit = _mm_packs_epi16(expired, expired);

	return ( _mm_movemask_epi8(expired_8bit) | ~b->mask_busy) & ENTRIES_MASK;
}

static inline void update_timer(struct rte_hash_hvariant_bucket * b, int i, uint16_t expirationTime){
	b->expire_date_timeunit[i] = expirationTime;
}

static inline void update_timer_if_needed(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * b, int i, uint16_t newExpirationTime, int updateExpirationTime, int ret_not_mask, int * ret, uint64_t *mask, int32_t mask_pos){
	if (updateExpirationTime){
		if(b->expire_date_timeunit[i] != newExpirationTime){
			b->expire_date_timeunit[i]=newExpirationTime;

			/* Entry changed slightly set iterated flag */
			unset_bit_in_mask(&b->mask_iterated_over, i);
			update_iter_idx(h, b);

			if(ret_not_mask){
				*ret = RHL_FOUND_UPDATED;
			}else{
				if(mask != NULL) *mask |= (1llu << mask_pos);
			}
		}
	}
}

#else
static inline int free_or_expired(const struct rte_hash_hvariant_bucket* b, int i, __rte_unused uint16_t currentTime){
	return (!get_bit_in_mask(&b->mask_busy,i));
}

static inline int matches_and_not_expired(const struct rte_hash_hvariant_bucket* b, int i, uint32_t hash, __rte_unused uint16_t currentTime){
	return (b->primary_signature_high[i] == (hash >> 16)) && get_bit_in_mask(&b->mask_busy,i);
}

static inline uint32_t matches_and_not_expired_maskpos(const struct rte_hash_hvariant_bucket* b, uint32_t hash, __rte_unused uint16_t currentTime){
	__m128i ref_hashes = _mm_set1_epi16(hash >> 16);
	__m128i bucket_hashes = _mm_load_si128((__m128i*)&b->primary_signature_high);
	__m128i eq_hash = _mm_cmpeq_epi16(bucket_hashes,ref_hashes);
	__m128i matches_8bit = _mm_packs_epi16(eq_hash, eq_hash);

	return _mm_movemask_epi8(matches_8bit) & b->mask_busy;
}

static inline uint32_t free_or_expired_maskpos(const struct rte_hash_hvariant_bucket* b, __rte_unused uint16_t currentTime){
	return (~b->mask_busy) & ENTRIES_MASK;
}

static inline void update_timer(__rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused int i, __rte_unused uint16_t expirationTime){}

static inline void update_timer_if_needed(__rte_unused struct rte_hash_hvariant * h, __rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused int i, __rte_unused uint16_t newExpirationTime, __rte_unused int updateExpirationTime, __rte_unused int ret_not_mask, __rte_unused int * ret, __rte_unused uint64_t *mask, __rte_unused int32_t mask_pos){}

#endif

// Changing this values implies checking the rest of the code
#define RTE_HASH_HORTON_REMAP_ENTRIES 21U
#define RTE_HASH_HORTON_REMAP_HASH 7U

#if HORTON
#include "jenkins_lookup3.c"
const uint32_t prime_number[] = {2147483647, 2147483629, 2147483587, 2147483579, 2147483563, 2147483549, 2147483543, 2147483497};

static inline uint32_t horton_tag(uint32_t sec_hash){
	// Sec hash is constrained so that the secondary bucket index is the same for two entries with same primary bucket and same tag
	// The high bits are unaffected by this constrant and thus remain unchaned overtime.
	return ((sec_hash >> 27U) * RTE_HASH_HORTON_REMAP_ENTRIES >> 5U); // Avoid Modulo // +/- equivalent to tag_hash modulo 21;
}
static inline uint32_t horton_sec_hash(struct rte_hash_hvariant * h, uint32_t prim_hash, uint32_t sec_hash, uint32_t hindex){
	uint32_t tag = horton_tag(sec_hash);
	const uint32_t ksec = (prim_hash & h->bucket_bitmask) * RTE_HASH_HORTON_REMAP_ENTRIES + (tag);
	const uint64_t hl1_ksec = hash_u32(ksec);
	const uint64_t hl2_ksec_hindex = prime_number[ksec & 0x07U]*hindex >> 3;
	const uint32_t horton_hash =  hl1_ksec + hl2_ksec_hindex;
	// Return a combinaison with low bits set to horton hash (bucket index -- should have correlation)
	//        and high bits set to sec_hash (to check equality -- should not have correlation -- and should keep tag unchanged)
	return (horton_hash & h->bucket_bitmask) | (sec_hash & ~h->bucket_bitmask);
}

static inline uint32_t horton_get_hindex_fromtag(struct rte_hash_hvariant_bucket * b, uint32_t tag){
	uint64_t offset = tag*3ULL;
	uint64_t hindex = (b->remap_array_21x3 >> offset) & 0x07;
	return hindex;
}


static inline uint32_t horton_get_hindex(struct rte_hash_hvariant_bucket * b, uint32_t sec_hash){
	uint32_t tag =  horton_tag(sec_hash);  // +/- equivalent to tag_hash modulo 21;
	return horton_get_hindex_fromtag(b,tag);
}



static inline void horton_set_hindex(struct rte_hash_hvariant_bucket * b, uint32_t sec_hash, uint32_t hindex){
	uint64_t tag =  horton_tag(sec_hash);  // +/- equivalent to tag_hash modulo 21;
	uint64_t offset = tag*3ULL;
	uint64_t ones_masked = (0x07ULL) << offset;
	uint64_t hindex_masked = (hindex & 0x07ULL) << offset;
	uint64_t new_remap = (b->remap_array_21x3 & ~ones_masked) | hindex_masked;
	b->remap_array_21x3 = new_remap;
}

/* Return 1 if all are full */
static inline uint32_t horton_choose_hindex(struct rte_hash_hvariant * h, uint32_t prim_hash, uint32_t sec_hash, uint16_t currentTime){
	uint32_t i;
	uint32_t hindex = 0;
	uint32_t free_at_hindex = 0;
	for(i=0;i<RTE_HASH_HORTON_REMAP_HASH;i++){
		uint32_t horton_hash = horton_sec_hash(h,prim_hash,sec_hash,i);
		struct rte_hash_hvariant_bucket * sec_bkt = &h->buckets[horton_hash & h->bucket_bitmask];
		uint32_t count_free = __popcntd(free_or_expired_maskpos(sec_bkt, currentTime));
		if(count_free > free_at_hindex){
			free_at_hindex = count_free;
			hindex = i;
		}
	}
	return hindex+1;

}

#else
static inline uint32_t horton_tag(__rte_unused uint32_t sec_hash){return 0;}
static inline uint32_t horton_sec_hash(__rte_unused struct rte_hash_hvariant * h, __rte_unused uint32_t prim_hash, __rte_unused uint32_t sec_hash, __rte_unused uint32_t hindex){return 0;}
static inline uint32_t horton_get_hindex(__rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused uint32_t sec_hash){return 0;}
static inline void horton_set_hindex(__rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused uint32_t sec_hash, __rte_unused uint32_t hindex){}
static inline uint32_t horton_choose_hindex(__rte_unused struct rte_hash_hvariant * h, __rte_unused uint32_t prim_hash, __rte_unused uint32_t sec_hash, __rte_unused uint16_t currentTime){return 0;}

#endif

static inline uint32_t primary_signature(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket *b, int i){
	return ((b->primary_signature_high[i] << 16) | (b - h->buckets));
}



struct rte_hash_hvariant *
H(rte_hash,create)(const struct rte_hash_hvariant_parameters *params)
{
	struct rte_hash_hvariant *h = NULL;
	char hash_name[RTE_HASH_HVARIANT_NAMESIZE];
	void *k = NULL;
	void *buckets = NULL;
	void *iter_group_mask = NULL;
	void *reset_group_mask = NULL;
	/**
	 * Initialize AVX constants
	 */
	max_expiration_time = _mm_set1_epi16(RTE_HASH_HVARIANT_MAX_EXPIRATION_PERIOD);


	/**
	 * Check that some invariants are valid
	 *  Altering this requires to alter the rest of the code (key computation, ...)
	 */
	assert(sizeof(hash_key_t) == 16); // i.e., key is _m128i type
	assert(sizeof(hash_data_t) == 16); // i.e., data is _m128i type
	//assert(RTE_HASH_HVARIANT_BUCKET_ENTRIES == 8); // i.e., number of bucket entries is exactly eight (for coherency with 8-bit  bitmasks)



	if (params == NULL) {
		RTE_LOG(ERR, HASH, "rte_hash_hvariant_create has no parameters\n");
		return NULL;
	}

	/* Check for valid parameters */
	if ((params->entries > RTE_HASH_HVARIANT_ENTRIES_MAX) ||
			(params->entries < RTE_HASH_HVARIANT_BUCKET_ENTRIES) ||
			!rte_is_power_of_2(RTE_HASH_HVARIANT_BUCKET_ENTRIES) ) {
		rte_errno = EINVAL;
		RTE_LOG(ERR, HASH, "rte_hash_hvariant_create has invalid parameters\n");
		return NULL;
	}


	snprintf(hash_name, sizeof(hash_name), "HT_%s", params->name);

	h = (struct rte_hash_hvariant *)rte_zmalloc_socket(hash_name, sizeof(struct rte_hash_hvariant),
					RTE_CACHE_LINE_SIZE, params->socket_id);

	if (h == NULL) {
		RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err;
	}

	const uint32_t num_buckets = RTE_MAX( 65536u , rte_align32pow2(params->entries) / RTE_HASH_HVARIANT_BUCKET_ENTRIES);

	buckets = rte_zmalloc_socket(NULL,
				num_buckets * sizeof(struct rte_hash_hvariant_bucket),
				RTE_CACHE_LINE_SIZE, params->socket_id);

	if (buckets == NULL) {
		RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err;
	}

	const uint32_t key_entry_size = sizeof(struct rte_hash_hvariant_key);
	const uint32_t num_key_slots = num_buckets*RTE_HASH_HVARIANT_BUCKET_ENTRIES +1 ; // Include one padding key slot as reads during batched lookups can read some dummy information
	const uint64_t hash_key_tbl_size = (uint64_t) key_entry_size * num_key_slots;

	k = rte_zmalloc_socket(NULL, hash_key_tbl_size,
			RTE_CACHE_LINE_SIZE, params->socket_id);

	if (k == NULL) {
		RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err;
	}

	iter_group_mask = rte_zmalloc_socket(NULL, num_buckets/ITERATOR_GROUP/64*sizeof(uint64_t),
			RTE_CACHE_LINE_SIZE, params->socket_id);

	if (iter_group_mask == NULL) {
		RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err;
	}


	reset_group_mask = rte_zmalloc_socket(NULL, num_buckets/ITERATOR_GROUP/64*sizeof(uint64_t),
				RTE_CACHE_LINE_SIZE, params->socket_id);

		if (reset_group_mask == NULL) {
			RTE_LOG(ERR, HASH, "memory allocation failed\n");
			goto err;
		}



	/* Setup hash context */
	snprintf(h->name, sizeof(h->name), "%s", params->name);
	h->entries = num_buckets*RTE_HASH_HVARIANT_BUCKET_ENTRIES;

	h->num_buckets = num_buckets;
	h->bucket_bitmask = h->num_buckets - 1;
	h->buckets = buckets;
	h->key_store = k;
	h->iter_group_bucket_mask = iter_group_mask;
	h->reset_group_bucket_mask = reset_group_mask;

	H(rte_hash,reset)(h);

	return h;
err:
	rte_free(h);
	rte_free(buckets);
	rte_free(k);
	rte_free(iter_group_mask);
	rte_free(reset_group_mask);
	return NULL;
}

void
H(rte_hash,free)(struct rte_hash_hvariant *h)
{

	if (h == NULL)
		return;

	rte_free(h->key_store);
	rte_free(h->buckets);
	rte_free(h->iter_group_bucket_mask);
	rte_free(h->reset_group_bucket_mask);
	rte_free(h);
}

/* Compute a 64bit hash for a 128 bit key .
 * The low 32 bits will be used as primary hash and the high 32 bits used as secondary hash */
static inline uint64_t
rte_hash_m128i(const hash_key_t key)
{
	return dcrc_hash_m128(key);
}


/* Compare two keys */
static inline int
rte_cmp_eq_m128i(const hash_key_t key1, const hash_key_t key2)
{
#if 0
	__m128i vcmp = _mm_xor_si128(key1.mm, key2.mm);        // PXOR
	return _mm_testz_si128(vcmp, vcmp);
#else
	return key1.a == key2.a && key1.b == key2.b;
#endif
}


void
H(rte_hash,reset)(struct rte_hash_hvariant *h)
{

	if (h == NULL)
		return;

	memset(h->buckets, 0, h->num_buckets * sizeof(struct rte_hash_hvariant_bucket));
	memset(h->key_store, 0, sizeof(struct rte_hash_hvariant_key) * (h->entries + 1));
}

static inline uint64_t bloom_mask_64(uint32_t sec_sig){
	uint64_t h1 = sec_sig & 0x3f;
	uint64_t h2 = sec_sig >> 6 & 0x3f;
	//uint64_t h3 = sec_sig >> 12 & 0x3f;
	//uint64_t h4 = sec_sig >> 18 & 0x3f;
	return (1ull << h1) | (1ull << h2 ); // | (1ull << h3) | (1ull << h4);
}

#if BLOOM
static inline void mark_as_secondary(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * b, uint32_t i, uint32_t sec_sig,__rte_unused  uint32_t hindex){
	// Mark as stored in secondary bucket
	set_bit_in_mask(&b->mask_in_secondary_position,i);

	// Find corresponding primary bucket
	struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[b->secondary_signature_full[i] & h->bucket_bitmask];

	// Increase counter moved_to_secondary, add to bloom filter
	prim_bucket->count_moved_to_secondary++;
	prim_bucket->bloom_moved |= bloom_mask_64(sec_sig);


}

static inline void reset_bucket_entry_bloom(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * b, uint32_t i, __rte_unused uint16_t currentTime){
	if(get_bit_in_mask(&b->mask_in_secondary_position,i)){
		// Find corresponding primary bucket
		struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[b->secondary_signature_full[i] & h->bucket_bitmask];

		// Decrease counter moved_to_secondary - if counter is zero then reset bloom filter
		prim_bucket->count_moved_to_secondary--;
		if(prim_bucket->count_moved_to_secondary == 0){
			prim_bucket->bloom_moved = 0;
		}

		unset_bit_in_mask(&b->mask_in_secondary_position,i);
	}
}
#elif HORTON

static inline void mark_as_secondary(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * b, uint32_t i, __rte_unused uint32_t sec_sig, uint32_t hindex){
	// Mark as stored in secondary bucket
	set_bit_in_mask(&b->mask_in_secondary_position,i);

	// Find corresponding primary bucket
	struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[b->secondary_signature_full[i] & h->bucket_bitmask];

	// Find the tag
	uint32_t prim_sig = primary_signature(h,b,i);

	//uint32_t previous_index = horton_get_hindex(prim_bucket, prim_sig);
	//if(previous_index != 0 && previous_index != hindex){
	//	printf("Incoherent horton indexes\n");
	//}

	// Set the hindex
	horton_set_hindex(prim_bucket, prim_sig, hindex);

}

static inline void reset_bucket_entry_bloom(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * b, uint32_t i,uint16_t currentTime){
	if(get_bit_in_mask(&b->mask_in_secondary_position,i)){
		// First, unset bit in mask in secondary position
		unset_bit_in_mask(&b->mask_in_secondary_position,i);

		// Find corresponding primary bucket
		struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[b->secondary_signature_full[i] & h->bucket_bitmask];

		// Find if another entry of bucket b was remapped from prim_bucket
		uint32_t j;
		for(j=0;j<RTE_HASH_HVARIANT_BUCKET_ENTRIES;j++){
			if(i != j && !free_or_expired(b,j,currentTime) && get_bit_in_mask(&b->mask_in_secondary_position,j)){
				int same_primbkt = (b->secondary_signature_full[i] & h->bucket_bitmask) == (b->secondary_signature_full[j] & h->bucket_bitmask);
				int same_tag =  horton_tag(primary_signature(h,b,i)) == horton_tag(primary_signature(h,b,j));
				if(same_primbkt && same_tag) return;
			}
		}


		// If no return in loop then no other entry was displaced from prim_bucket using that tag, reset tag to zero
		horton_set_hindex(prim_bucket, primary_signature(h,b,i), 0);
	}
}

#else
static inline void mark_as_secondary(__rte_unused struct rte_hash_hvariant * h, __rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused uint32_t i, __rte_unused uint32_t sec_sig, __rte_unused uint32_t hindex){}
static inline void reset_bucket_entry_bloom(__rte_unused struct rte_hash_hvariant * h, __rte_unused struct rte_hash_hvariant_bucket * b, __rte_unused uint32_t i, __rte_unused uint16_t currentTime){}
#endif

static inline void move_bucket_entry(struct rte_hash_hvariant * h, struct rte_hash_hvariant_bucket * bfrom, int ifrom, struct rte_hash_hvariant_bucket * bto, int ito, __rte_unused uint32_t hindex, uint16_t currentTime){
	/* Update bloom filter before overwritting information in bucket "to" */
	reset_bucket_entry_bloom(h,bto,ito, currentTime);

	uint32_t to_secondary = !get_bit_in_mask(&bfrom->mask_in_secondary_position,ifrom);

	/* Recompute hashes */
	uint32_t sec_sig = primary_signature(h,bfrom,ifrom);
    uint32_t prim_sig = bfrom->secondary_signature_full[ifrom];


    /* Compute key positions */
	int key_from = (bfrom - h->buckets) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + ifrom;
	int key_to = (bto - h->buckets) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + ito;

	/* Write in new position */
#if HORTON
	if(to_secondary){
		// If writing to secondry, update hash according to hindex
		prim_sig = horton_sec_hash(h,sec_sig,prim_sig,hindex);
	}else{
	}
#endif



	bto->primary_signature_high[ito] = prim_sig >> 16;
	bto->secondary_signature_full[ito] = sec_sig;

#if TIMER
	bto->expire_date_timeunit[ito] =  bfrom->expire_date_timeunit[ifrom];
#endif
	set_bit_in_mask(&bto->mask_busy,ito);
	h->key_store[key_to] = h->key_store[key_from]; // Move data

	/* Set iterated bits and update iterator*/
	if(! get_bit_in_mask(&bfrom->mask_iterated_over,ifrom)){
		update_iter_idx(h,bto);
	}
#ifdef FAST_RESET_ITERATOR
	uint32_t group_from_need_iterator_reset = get_bit_in_largemask(h->reset_group_bucket_mask, (bfrom - h->buckets)/ITERATOR_GROUP);
	uint32_t group_to_need_iterator_reset = get_bit_in_largemask(h->reset_group_bucket_mask, (bto - h->buckets)/ITERATOR_GROUP);
	if(group_to_need_iterator_reset){
		reset_iterator_group(h,(bto - h->buckets)/ITERATOR_GROUP);
	}
	if(group_from_need_iterator_reset){
		unset_bit_in_mask(&bto->mask_iterated_over, ito);
	}else{
		copy_bit_in_mask(&bfrom->mask_iterated_over,&bto->mask_iterated_over,ifrom,ito);
	}
#else
	copy_bit_in_mask(&bfrom->mask_iterated_over,&bto->mask_iterated_over,ifrom,ito);
#endif

	/* Update bloom filter */
	if(to_secondary){
		//if(HORTON && hindex ==0) printf("hindex is 0!!!\n");
		mark_as_secondary(h,bto,ito,prim_sig, hindex);
	}

	/* Restore "free" bucket entry -- not needed as it will be overwritten*/
    //unset_bit_in_mask(&bfrom->mask_busy,ifrom);
	//reset_bucket_entry_bloom(h,bfrom,ifrom);

}

static inline int
make_space_bucket(struct rte_hash_hvariant *h, struct rte_hash_hvariant_bucket *bkt, uint16_t currentTime)
{
	struct rte_hash_hvariant_bucket *current_bucket[RTE_HASH_HVARIANT_MAX_RECURSION+1];
	int current_slot[RTE_HASH_HVARIANT_MAX_RECURSION+1];
	uint32_t current_hindex[RTE_HASH_HVARIANT_MAX_RECURSION+1];
	int current_level=0;

	current_bucket[0] = bkt;

	/* Search a cuckoo path */
	while(current_level < RTE_HASH_HVARIANT_MAX_RECURSION){
		struct rte_hash_hvariant_bucket * bkt = current_bucket[current_level];

		/* Prefetch the key to be swapped later */
		if(current_level >= 1){
			rte_prefetch0(&h->key_store[(current_bucket[current_level-1] - h->buckets) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + current_slot[current_level-1]]);
		}

		/* Search for an item whose secondary bucket has free space */
		unsigned i;
		struct rte_hash_hvariant_bucket * next_bkt[RTE_HASH_HVARIANT_BUCKET_ENTRIES];
		uint32_t hindex[RTE_HASH_HVARIANT_BUCKET_ENTRIES] = {0};

		for(i = 0; i < RTE_HASH_HVARIANT_BUCKET_ENTRIES; i++){
			int bi;
			if(HORTON && !get_bit_in_mask(&bkt->mask_in_secondary_position,i)){
				// Go to secondary bucket (horton case)
				uint32_t prim_hash = primary_signature(h,bkt,i);
				hindex[i] = horton_get_hindex(bkt,bkt->secondary_signature_full[i]);
				if(hindex[i] == 0){
					// Hindex not yet defined, define new hindex
					hindex[i] = horton_choose_hindex(h,prim_hash, bkt->secondary_signature_full[i], currentTime);
				}
				uint32_t horton_hash = horton_sec_hash(h, prim_hash, bkt->secondary_signature_full[i], hindex[i]);
				bi = horton_hash & h->bucket_bitmask;
			}else{
				bi = bkt->secondary_signature_full[i] & h->bucket_bitmask;
				hindex[i] = 0;
			}
			next_bkt[i] = &h->buckets[bi];
			rte_prefetch0(next_bkt);
		}
		for(i = 0; i < RTE_HASH_HVARIANT_BUCKET_ENTRIES; i++){
			uint32_t free_entries = free_or_expired_maskpos(next_bkt[i],currentTime);
			if(free_entries){
				current_slot[current_level] = i;
				current_slot[current_level+1] = __builtin_ctz(free_entries);
				current_bucket[current_level+1] = next_bkt[i] ;
				current_hindex[current_level+1] = hindex[i] ;
				current_level++;
				goto found_path;
			}
		}

		/* Pick the next not yet considered item if possible */
		if(bkt->mask_already_considered_for_swap != ENTRIES_MASK){
			int j = __builtin_ctz( (~bkt->mask_already_considered_for_swap) & ENTRIES_MASK );
			current_slot[current_level] = j;
			current_bucket[current_level+1] = next_bkt[j];
			current_hindex[current_level+1] = hindex[j] ;
			set_bit_in_mask(&bkt->mask_already_considered_for_swap,j);
			current_level++;
		}else{
			goto no_path_found;
		}

	}

	/* If no substitution path was found, return */
	no_path_found:
		while(current_level >= 1){
		    current_level--;
		    struct rte_hash_hvariant_bucket * bkt = current_bucket[current_level];
		    bkt->mask_already_considered_for_swap = 0;
		}
		return -ENOSPC;

	/* Apply the cuckoo path */
	found_path:;
		//if(current_level > 2) printf("%d\n", current_level);
		while(current_level >= 1){
			struct rte_hash_hvariant_bucket * bkt_to = current_bucket[current_level];
			int slot_to = current_slot[current_level];
			uint32_t hindex = current_hindex[current_level];
			current_level--;
			struct rte_hash_hvariant_bucket * bkt_from = current_bucket[current_level];
			int slot_from = current_slot[current_level];
			bkt_from->mask_already_considered_for_swap = 0;
			move_bucket_entry(h,bkt_from,slot_from,bkt_to,slot_to,hindex, currentTime);
		}
		return current_slot[0];
}

static inline int32_t
__rte_hash_hvariant_add_key_with_hash(struct rte_hash_hvariant *h, const hash_key_t key,
						uint64_t sig64, hash_data_t data, uint16_t expirationTime, uint16_t currentTime)
{
	uint32_t prim_hash,sec_hash;
	uint32_t prim_bucket_idx, sec_bucket_idx;
	int i;
	uint64_t tmp;
	struct rte_hash_hvariant_bucket *prim_bkt, *sec_bkt;
	struct rte_hash_hvariant_key *new_k, *k, *keys = h->key_store;
	uint32_t new_idx;

	prim_hash = sig64; // Discards 32 high bits
	prim_bucket_idx = prim_hash & h->bucket_bitmask;
	prim_bkt = &h->buckets[prim_bucket_idx];

	// Prefetch second cacheline of primary bucket if need (i.e., using timers)
	if(sizeof(struct rte_hash_hvariant_bucket) > 64) rte_prefetch0(((char*)prim_bkt)+64);

	sec_hash = sig64 >> 32;

#if BLOOM
	/* If bloom filter does not matches, we don't need to check the secondary bucket */
	uint64_t bloom = bloom_mask_64(sec_hash);
	int could_be_in_secondary = (prim_bkt->bloom_moved & bloom) == bloom;
#elif HORTON
	/* If horton does not match, we don't need to check the secondary bucket -- also compute the horton hash to know which bucket is the secondary */
	uint32_t could_be_in_secondary = horton_get_hindex(prim_bkt,sec_hash);
	// Update secondary hash to use
	sec_hash = horton_sec_hash(h,prim_hash,sec_hash,could_be_in_secondary);
#else
	/* No optimization always check */
	int could_be_in_secondary = 1;
#endif
	sec_bucket_idx = sec_hash & h->bucket_bitmask;
	sec_bkt = &h->buckets[sec_bucket_idx];


	if(could_be_in_secondary) rte_prefetch0(sec_bkt);


	/* Check if key is already inserted in primary location */
	uint32_t prim_matches = matches_and_not_expired_maskpos(prim_bkt, prim_hash, currentTime);
	FOREACH_IN_MASK32(i, prim_matches, tmp){
			k = &keys[prim_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
			if (rte_cmp_eq_m128i(key, k->key)) {
				/* Update data */
				k->data = data;

				/* Update expiration time */
				update_timer(prim_bkt,i,expirationTime);

				/* Reset iterated flag */
				unset_bit_in_mask(&prim_bkt->mask_iterated_over, i);
				update_iter_idx(h, prim_bkt);

				/*
				 * Return index where key is stored,
				 * substracting the first dummy index
				 */
				return RHL_FOUND_UPDATED;
		    }
	}


	/* Check if key is already inserted in secondary location */
	if(could_be_in_secondary){
		uint32_t sec_matches = matches_and_not_expired_maskpos(sec_bkt, sec_hash, currentTime);
		FOREACH_IN_MASK32(i, sec_matches, tmp){
				k = &keys[sec_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
				if (rte_cmp_eq_m128i(key, k->key)) {
					/* Update data */
					k->data = data;

					/* Update expiration time */
					update_timer(sec_bkt,i,expirationTime);

					/* Reset iterated flag */
					unset_bit_in_mask(&sec_bkt->mask_iterated_over, i);
					update_iter_idx(h,sec_bkt);

					/*
					 * Return index where key is stored,
					 * substracting the first dummy index
					 */
					return RHL_FOUND_UPDATED;
				}
		}
	}

	/* Insert new entry if there is room in the primary bucket */
	int prim_free, sec_free;
	if(0 != (prim_free =  free_or_expired_maskpos(prim_bkt, currentTime))){
		i = __builtin_ctzl(prim_free);

		/* Get the slot for storing the key (use the one in place)*/
		new_idx = prim_bucket_idx* RTE_HASH_HVARIANT_BUCKET_ENTRIES + i;
		new_k = &keys[new_idx];
		rte_prefetch0(new_k);

		/* Erase previous value if needed (Update bloom filter) */
		reset_bucket_entry_bloom(h,prim_bkt,i, currentTime);

		/* Mark as busy */
		set_bit_in_mask(&prim_bkt->mask_busy,i);

		/* Update signatures */
		prim_bkt->primary_signature_high[i] = prim_hash >> 16;
		prim_bkt->secondary_signature_full[i] = sec_hash;

		/* Update expiration time */
		update_timer(prim_bkt,i,expirationTime);

		/* Reset iterated flag */
		unset_bit_in_mask(&prim_bkt->mask_iterated_over, i);
		update_iter_idx(h,prim_bkt);
	}else{
#if HORTON
		/* In case of Horton hashtable we first need to find the hindex (lowest loaded secondary bucket possible)*/
		uint32_t hindex = horton_get_hindex(prim_bkt,sec_hash);
		if(hindex == 0){
			hindex = horton_choose_hindex(h,prim_hash, sec_hash, currentTime);
		}
		sec_hash = horton_sec_hash(h,prim_hash, sec_hash, hindex);
		sec_bucket_idx = sec_hash & h->bucket_bitmask;
		sec_bkt = &h->buckets[sec_bucket_idx];
#else
		uint32_t hindex=0;
#endif
		if(0 != (sec_free =  free_or_expired_maskpos(sec_bkt, currentTime))){
			i = __builtin_ctzl(sec_free);

			/* Get the slot for storing the key (use the one in place)*/
			new_idx = sec_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i;
			new_k = &keys[new_idx];
			rte_prefetch0(new_k);

			/* Update bloom filter */
			reset_bucket_entry_bloom(h,sec_bkt,i, currentTime);

			/* Mark as busy */
			set_bit_in_mask(&sec_bkt->mask_busy,i);

			/* Update signatures */
			sec_bkt->primary_signature_high[i] = sec_hash >> 16;
			sec_bkt->secondary_signature_full[i] = prim_hash;


			/* Update expiration time */
			update_timer(sec_bkt,i,expirationTime);

			/* Reset iterated flag */
			unset_bit_in_mask(&sec_bkt->mask_iterated_over, i);
			update_iter_idx(h, sec_bkt);

			/* Mark as stored in secondary and update bloom filter */
			mark_as_secondary(h,sec_bkt,i,sec_hash,hindex);
		}else if(0 <= (i = make_space_bucket(h, prim_bkt, currentTime))){
			/* Primary bucket is full, so we need to make space in it for new entry , if found insert*/

			/* Get the slot for storing the key (use the one in place)*/
			new_idx = prim_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i;
			new_k = &keys[new_idx];
			rte_prefetch0(new_k);

			/* Update bloom filter */
			reset_bucket_entry_bloom(h,prim_bkt,i, currentTime);

			/* Mark as busy */
			set_bit_in_mask(&prim_bkt->mask_busy,i);

			/* Bucket */
			prim_bkt->primary_signature_high[i] = prim_hash >> 16;
			prim_bkt->secondary_signature_full[i] = sec_hash;

			/* Update expiration time */
			update_timer(prim_bkt,i,expirationTime);

			/* Reset iterated flag */
			unset_bit_in_mask(&sec_bkt->mask_iterated_over, i);
			update_iter_idx(h, prim_bkt);
		}else{
			return -ENOSPC;
		}
	}
	/* Copy key */
	new_k->key = key;
	new_k->data = data;
	return RHL_FOUND_UPDATED;
}

int32_t
H(rte_hash,add_key_with_hash)(struct rte_hash_hvariant *h,
			const hash_key_t key, hash_sig64_t sig,uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	const struct rte_tch_data zero_data = {.a=0,.b=0};
	return __rte_hash_hvariant_add_key_with_hash(h, key, sig, zero_data, expirationTime, currentTime);
}

int32_t
H(rte_hash,add_key)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	const struct rte_tch_data zero_data = {.a=0,.b=0};
	return __rte_hash_hvariant_add_key_with_hash(h, key, rte_hash_m128i(key),  zero_data, expirationTime, currentTime);
}

int
H(rte_hash,add_key_with_hash_data)(struct rte_hash_hvariant *h,
			const hash_key_t key, hash_sig64_t sig, hash_data_t data, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_add_key_with_hash(h, key, sig, data, expirationTime, currentTime);
}

int
H(rte_hash,add_key_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t data, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_add_key_with_hash(h, key, rte_hash_m128i(key), data, expirationTime, currentTime);

}
static inline int32_t
__rte_hash_hvariant_lookup_with_hash(struct rte_hash_hvariant *h, const hash_key_t key,
					uint64_t sig64, hash_data_t *data, uint16_t currentTime, int updateExpirationTime, uint16_t newExpirationTime)
{
	uint32_t prim_bucket_idx, sec_bucket_idx;
	uint32_t prim_hash,sec_hash;

	unsigned i, tmp;
	struct rte_hash_hvariant_bucket *prim_bkt, *sec_bkt;
	struct rte_hash_hvariant_key *k, *keys = h->key_store;
	int ret = RHL_FOUND_NOTUPDATED;

	prim_hash = sig64;  // Discard 32 high bits.
	prim_bucket_idx = prim_hash & h->bucket_bitmask;
	prim_bkt = &h->buckets[prim_bucket_idx];

	//rte_prefetch0(sec_bkt);

	/* Check if key is in primary location */
	uint32_t prim_matches = matches_and_not_expired_maskpos(prim_bkt, prim_hash, currentTime);
	FOREACH_IN_MASK32(i, prim_matches, tmp){
			k = &keys[prim_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
			if (rte_cmp_eq_m128i(key, k->key)) {
				update_timer_if_needed(h,prim_bkt,i,newExpirationTime,updateExpirationTime,1,&ret,NULL,0);

				if (data != NULL)
					*data = k->data;
				/*
				 * Return status code
				 */
				return ret;
			}
	}

	sec_hash = sig64 >> 32;
#if BLOOM
	/* If bloom filter does not matches, we don't need to check the secondary bucket */
	uint64_t bloom = bloom_mask_64(sec_hash);
	int could_be_in_secondary = (prim_bkt->bloom_moved & bloom) == bloom;
#elif HORTON
	/* If horton does not match, we don't need to check the secondary bucket -- also compute the horton hash to know which bucket is the secondary */
	uint32_t could_be_in_secondary = horton_get_hindex(prim_bkt,sec_hash);
	// Update secondary hash to use
	sec_hash = horton_sec_hash(h,prim_hash,sec_hash,could_be_in_secondary);
#else
	/* No optimization always check */
	int could_be_in_secondary = 1;
#endif
	/* Early stop if filter (horton or bloom) matches */
	if(!could_be_in_secondary){
		return -ENOENT;
	}
	sec_bucket_idx = sec_hash & h->bucket_bitmask;
	sec_bkt = &h->buckets[sec_bucket_idx];



	/* Check if key is in secondary location */
	uint32_t sec_matches = matches_and_not_expired_maskpos(sec_bkt, sec_hash, currentTime);
	FOREACH_IN_MASK32(i, sec_matches, tmp){
			k = &keys[sec_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
			if (rte_cmp_eq_m128i(key, k->key)) {
				update_timer_if_needed(h,sec_bkt,i,newExpirationTime,updateExpirationTime,1,&ret,NULL,0);

				if (data != NULL)
					*data = k->data;
				/*
				 * Return status code
				 */
				return ret;
			}
	}

	return -ENOENT;
}

int32_t
H(rte_hash,lookup_with_hash)(struct rte_hash_hvariant *h,
			const hash_key_t key, uint64_t sig, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, sig, NULL, currentTime,0, 0);
}

int32_t
H(rte_hash,lookup)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, rte_hash_m128i(key), NULL, currentTime,0, 0);
}

int
H(rte_hash,lookup_with_hash_data)(struct rte_hash_hvariant *h,
			const hash_key_t key, uint64_t sig, hash_data_t  *data, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, sig, data, currentTime, 0, 0);
}

int
H(rte_hash,lookup_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t  *data, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, rte_hash_m128i(key), data, currentTime, 0, 0);
}

int32_t
H(rte_hash,lookup_update_with_hash)(struct rte_hash_hvariant *h,
			const hash_key_t key, uint64_t sig, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, sig, NULL, currentTime, -1,expirationTime);
}

int32_t
H(rte_hash,lookup_update)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, rte_hash_m128i(key), NULL, currentTime, -1, expirationTime);
}

int
H(rte_hash,lookup_update_with_hash_data)(struct rte_hash_hvariant *h,
			const hash_key_t key, uint64_t sig, hash_data_t *data, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, sig, data, currentTime, -1, expirationTime);
}

int
H(rte_hash,lookup_update_data)(struct rte_hash_hvariant *h, const hash_key_t key, hash_data_t  *data, uint16_t expirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_lookup_with_hash(h, key, rte_hash_m128i(key), data, currentTime, -1, expirationTime);
}

static inline int32_t
__rte_hash_hvariant_del_key_with_hash(struct rte_hash_hvariant *h, const hash_key_t key,
						uint64_t sig64, uint16_t currentTime)
{
	uint32_t prim_bucket_idx, sec_bucket_idx;
	uint32_t prim_hash, sec_hash;
	unsigned i,tmp;
	struct rte_hash_hvariant_bucket *prim_bkt , *sec_bkt;
	struct rte_hash_hvariant_key *k, *keys = h->key_store;

	prim_hash = sig64 ;  // Discard 32 high bits.
	prim_bucket_idx = prim_hash & h->bucket_bitmask;
	prim_bkt = &h->buckets[prim_bucket_idx];

	/* Check if key is in primary location */
	uint32_t prim_matches = matches_and_not_expired_maskpos(prim_bkt, prim_hash, currentTime);
	FOREACH_IN_MASK32(i, prim_matches, tmp){
			k = &keys[prim_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
			if (rte_cmp_eq_m128i(key, k->key)) {
				/* Mark entry as free */
				unset_bit_in_mask(&prim_bkt->mask_busy,i);

				/*
				 * Return status code
				 */
				return RHL_FOUND_UPDATED;
			}
	}

	/* Calculate secondary hash */
	sec_hash = sig64 >> 32;
#if BLOOM
	/* If bloom filter does not matches, we don't need to check the secondary bucket */
	uint64_t bloom = bloom_mask_64(sec_hash);
	int could_be_in_secondary = (prim_bkt->bloom_moved & bloom) == bloom;
#elif HORTON
	/* If horton does not match, we don't need to check the secondary bucket -- also compute the horton hash to know which bucket is the secondary */
	uint32_t could_be_in_secondary = horton_get_hindex(prim_bkt,sec_hash);
	// Update secondary hash to use
	sec_hash = horton_sec_hash(h,prim_hash,sec_hash,could_be_in_secondary);
#else
	/* No optimization always check */
	int could_be_in_secondary = 1;
#endif
	/* Early stop if filter (horton or bloom) matches */
	if(!could_be_in_secondary){
		return -ENOENT;
	}

	sec_bucket_idx = sec_hash & h->bucket_bitmask;
	sec_bkt = &h->buckets[sec_bucket_idx];

	/* Check if key is in secondary location */
	uint32_t sec_matches = matches_and_not_expired_maskpos(sec_bkt, sec_hash, currentTime);
	FOREACH_IN_MASK32(i, sec_matches, tmp){
			k = &keys[sec_bucket_idx * RTE_HASH_HVARIANT_BUCKET_ENTRIES + i];
			if (rte_cmp_eq_m128i(key, k->key)) {
				/* Update bloom filter */
				reset_bucket_entry_bloom(h,prim_bkt,i, currentTime);

				/* Mark entry as free */
				unset_bit_in_mask(&sec_bkt->mask_busy,i);

				/*
				 * Return status code
				 */
				return RHL_FOUND_UPDATED;
			}
	}

	return -ENOENT;
}

int32_t
H(rte_hash,del_key_with_hash)(struct rte_hash_hvariant *h,
			const hash_key_t key, hash_sig64_t sig, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_del_key_with_hash(h, key, sig, currentTime);
}

int32_t
H(rte_hash,del_key)(struct rte_hash_hvariant *h, const hash_key_t key, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
	return __rte_hash_hvariant_del_key_with_hash(h, key, rte_hash_m128i(key), currentTime);
}


static inline void
__rte_hash_hvariant_lookup_bulk(struct rte_hash_hvariant *h, const hash_key_t *keys,
			uint64_t lookup_mask_query, uint64_t *hit_mask,  uint64_t * updated_mask,
			hash_data_t data[], uint16_t currentTime, uint16_t* newExpirationTime, uint16_t updateExpirationTime)
{
	uint64_t hits = 0, tmpm;
	int32_t i;
	uint32_t prim_hash[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	uint32_t sec_hash[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	struct rte_hash_hvariant_bucket *primary_bkt[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	struct rte_hash_hvariant_bucket *secondary_bkt[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	uint32_t prim_hitmask[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	uint32_t sec_hitmask[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	uint32_t could_be_in_secondary[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	//uint32_t prefetch_secondary[RTE_HASH_HVARIANT_LOOKUP_BULK_MAX];
	//uint32_t at_least_one_in_secondary = 0;

	if(lookup_mask_query == 0xffffffffULL){
		const int32_t num_keys=32;

		/* Calculate and prefetch rest of the buckets */
		for (i = 0; i < num_keys; i++) {
			uint64_t hash = dcrc_hash_m128(keys[i]);
			prim_hash[i] = hash;
			sec_hash[i] = hash >> 32;

			primary_bkt[i] = &h->buckets[prim_hash[i] & h->bucket_bitmask];
			rte_prefetch0(primary_bkt[i]);

#if UNCONDITIONAL_PREFETCH
			secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
			rte_prefetch0(secondary_bkt[i]);
#endif
		}

		/* Compare signatures and prefetch key slot of first hit */
		for (i = 0; i < num_keys; i++) {
			prim_hitmask[i] = matches_and_not_expired_maskpos(primary_bkt[i], prim_hash[i], currentTime);
#if UNCONDITIONAL_PREFETCH
			sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
#endif
#if BLOOM
			uint64_t bloom = bloom_mask_64(sec_hash[i]);
			could_be_in_secondary[i] =  (primary_bkt[i]->bloom_moved &  bloom) == bloom ;
#elif HORTON
			could_be_in_secondary[i] = horton_get_hindex(primary_bkt[i],sec_hash[i]);
#endif

			/* Prefetch the primary key slot */
			if (prim_hitmask[i]) {
				uint32_t first_hit = __builtin_ctz(prim_hitmask[i]);
				uint32_t key_idx = (prim_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}

			/* Prefetch the secondary bucket */
			if((BLOOM && could_be_in_secondary[i]) ||
			   (CONDITIONAL_PREFETCH && 0 == prim_hitmask[i])){
				secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
				rte_prefetch0(secondary_bkt[i]);
			}

			/* Prefetch the secondary bucket (horton case)*/
			if(HORTON && could_be_in_secondary[i]){
				uint32_t horton_hash = horton_sec_hash(h,prim_hash[i],sec_hash[i],could_be_in_secondary[i]);
				secondary_bkt[i] = &h->buckets[horton_hash & h->bucket_bitmask];
				// Secondary hash to use in the rest of the algorithm is derived from horton_hash
				sec_hash[i] = horton_hash;
			}

			/* Prefetch the secondary key slot */
			if(UNCONDITIONAL_PREFETCH && sec_hitmask[i] && 0 == prim_hitmask[i]) {
				uint32_t first_hit = __builtin_ctz(sec_hitmask[i]);
				uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}

		}

		/* If needed (high load factors) - process secondary buckets according to could_be_in_secondary
		 * and prefetch secondary keys if bloom filter matches / or primary bucket don't match) */
		for (i = 0; i < num_keys; i++) {
			if((BLOOM && could_be_in_secondary[i]) ||
			   (HORTON && could_be_in_secondary[i]) ||
			   (CONDITIONAL_PREFETCH && 0 == prim_hitmask[i])){
				//secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
				sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
				uint32_t first_hit = __builtin_ctz(sec_hitmask[i]);
				uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}
		}



		/* Compare keys, first hits in primary first */
		for (i = 0; i < num_keys; i++) {
			//positions[i] = -ENOENT;
			uint32_t hit_index, tmp;
			FOREACH_IN_MASK32(hit_index, prim_hitmask[i], tmp){
				uint32_t key_idx = (prim_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + hit_index;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];

				/* Access primary key slot */
				if (rte_cmp_eq_m128i(keys[i],key_slot->key)) {
					if (data != NULL)
						data[i] = key_slot->data;

					hits |= 1ULL << i;

					update_timer_if_needed(h, primary_bkt[i], hit_index, newExpirationTime[i],updateExpirationTime,0,NULL,updated_mask,i);

					goto next_key;
				}
			}

			if((BLOOM && could_be_in_secondary[i]) ||
			   (HORTON && could_be_in_secondary[i]) ||
			   ((!BLOOM) && (!HORTON))){
				if(NO_PREFETCH ||
				   (CONDITIONAL_PREFETCH && 0 != prim_hitmask[i])){
					/* Access secondary bucket */
					secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
					sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
				}
				FOREACH_IN_MASK32(hit_index, sec_hitmask[i], tmp){
					uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + hit_index;
					const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];

					/* Access secondary key slot */
					if (rte_cmp_eq_m128i(keys[i],key_slot->key)) {
						if (data != NULL)
							data[i] = key_slot->data;

						hits |= 1ULL << i;

						update_timer_if_needed(h, secondary_bkt[i], hit_index, newExpirationTime[i],updateExpirationTime,0,NULL,updated_mask,i);

						goto next_key;
					}
				}
			}

	next_key:
			continue;
		}

		if (hit_mask != NULL)
			*hit_mask = hits;
	}else{
		/* Calculate and prefetch rest of the buckets */
		FOREACH_IN_MASK64(i, lookup_mask_query, tmpm){
			uint64_t hash = dcrc_hash_m128(keys[i]);
			prim_hash[i] = hash;
			sec_hash[i] = hash >> 32;

			primary_bkt[i] = &h->buckets[prim_hash[i] & h->bucket_bitmask];
			rte_prefetch0(primary_bkt[i]);

#if UNCONDITIONAL_PREFETCH
			secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
			rte_prefetch0(secondary_bkt[i]);
#endif
		}

		/* Compare signatures and prefetch key slot of first hit */
		FOREACH_IN_MASK64(i, lookup_mask_query, tmpm){
			prim_hitmask[i] = matches_and_not_expired_maskpos(primary_bkt[i], prim_hash[i], currentTime);
#if UNCONDITIONAL_PREFETCH
			sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
#endif
#if BLOOM
			uint64_t bloom = bloom_mask_64(sec_hash[i]);
			could_be_in_secondary[i] =  (primary_bkt[i]->bloom_moved &  bloom) == bloom ;
#elif HORTON
			could_be_in_secondary[i] = horton_get_hindex(primary_bkt[i],sec_hash[i]);
#endif

			/* Prefetch the primary key slot */
			if (prim_hitmask[i]) {
				uint32_t first_hit = __builtin_ctz(prim_hitmask[i]);
				uint32_t key_idx = (prim_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}

			/* Prefetch the secondary bucket */
			if((BLOOM && could_be_in_secondary[i]) ||
			   (CONDITIONAL_PREFETCH && 0 == prim_hitmask[i])){
				secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
				rte_prefetch0(secondary_bkt[i]);
			}

			/* Prefetch the secondary bucket (horton case)*/
			if(HORTON && could_be_in_secondary[i]){
				uint32_t horton_hash = horton_sec_hash(h,prim_hash[i],sec_hash[i],could_be_in_secondary[i]);
				secondary_bkt[i] = &h->buckets[horton_hash & h->bucket_bitmask];
				// Secondary hash to use in the rest of the algorithm is derived from horton_hash
				sec_hash[i] = horton_hash;
			}

			/* Prefetch the secondary key slot */
			if(UNCONDITIONAL_PREFETCH && sec_hitmask[i] && 0 == prim_hitmask[i]) {
				uint32_t first_hit = __builtin_ctz(sec_hitmask[i]);
				uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}

		}

		/* If needed (high load factors) - process secondary buckets according to could_be_in_secondary
		 * and prefetch secondary keys if bloom filter matches / or primary bucket don't match) */
		{int tmpm2;
		FOREACH_IN_MASK64(i, lookup_mask_query, tmpm2){
			if((BLOOM && could_be_in_secondary[i]) ||
			   (HORTON && could_be_in_secondary[i]) ||
			   (CONDITIONAL_PREFETCH && 0 == prim_hitmask[i])){
				//secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
				sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
				uint32_t first_hit = __builtin_ctz(sec_hitmask[i]);
				uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + first_hit;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];
				rte_prefetch0(key_slot);
			}
		}
		}



		/* Compare keys, first hits in primary first */
		FOREACH_IN_MASK64(i, lookup_mask_query, tmpm){
			//positions[i] = -ENOENT;
			uint32_t hit_index, tmp;
			FOREACH_IN_MASK32(hit_index, prim_hitmask[i], tmp){
				uint32_t key_idx = (prim_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + hit_index;
				const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];

				/* Access primary key slot */
				if (rte_cmp_eq_m128i(keys[i],key_slot->key)) {
					if (data != NULL)
						data[i] = key_slot->data;

					hits |= 1ULL << i;

					update_timer_if_needed(h, primary_bkt[i], hit_index, newExpirationTime[i],updateExpirationTime,0,NULL,updated_mask,i);

					goto next_keyb;
				}
			}

			if((BLOOM && could_be_in_secondary[i]) ||
			   (HORTON && could_be_in_secondary[i]) ||
			   ((!BLOOM) && (!HORTON))){
				if(NO_PREFETCH ||
				   (CONDITIONAL_PREFETCH && 0 != prim_hitmask[i])){
					/* Access secondary bucket */
					secondary_bkt[i] = &h->buckets[sec_hash[i] & h->bucket_bitmask];
					sec_hitmask[i] = matches_and_not_expired_maskpos(secondary_bkt[i], sec_hash[i], currentTime);
				}
				FOREACH_IN_MASK32(hit_index, sec_hitmask[i], tmp){
					uint32_t key_idx = (sec_hash[i] & h->bucket_bitmask) * RTE_HASH_HVARIANT_BUCKET_ENTRIES + hit_index;
					const struct rte_hash_hvariant_key *key_slot = &h->key_store[key_idx];

					/* Access secondary key slot */
					if (rte_cmp_eq_m128i(keys[i],key_slot->key)) {
						if (data != NULL)
							data[i] = key_slot->data;

						hits |= 1ULL << i;

						update_timer_if_needed(h, secondary_bkt[i], hit_index, newExpirationTime[i],updateExpirationTime,0,NULL,updated_mask,i);

						goto next_keyb;
					}
				}
			}

	next_keyb:
			continue;
		}

		if (hit_mask != NULL)
			*hit_mask = hits;



	}
}


int
H(rte_hash,lookup_bulk_data)(struct rte_hash_hvariant *h, const hash_key_t *keys,
		      uint32_t num_keys, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (keys == NULL) || (num_keys == 0) ||
			(num_keys > RTE_HASH_HVARIANT_LOOKUP_BULK_MAX) ||
			(hit_mask == NULL)), -EINVAL);

	uint64_t lookup_mask = (uint64_t) -1 >> (64 - num_keys);

	__rte_hash_hvariant_lookup_bulk(h, keys, lookup_mask, hit_mask, NULL, data,currentTime,0,0);

	/* Return number of hits */
	return __builtin_popcountl(*hit_mask);
}

int
H(rte_hash,lookup_bulk_data_mask)(struct rte_hash_hvariant *h, const hash_key_t *keys,
		      uint64_t lookup_mask, uint64_t *hit_mask, hash_data_t data[], uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (keys == NULL)  || (hit_mask == NULL)), -EINVAL);

	if(lookup_mask == 0){
		*hit_mask=0;
		return 0;
	}
	__rte_hash_hvariant_lookup_bulk(h, keys, lookup_mask, hit_mask, NULL, data,currentTime,0,0);
	return 0;
}


int
H(rte_hash,lookup_update_bulk_data_mask)(struct rte_hash_hvariant *h, const hash_key_t *keys,
		      uint64_t lookup_mask, uint64_t *hit_mask, uint64_t * updated_mask, hash_data_t data[], uint16_t * newExpirationTime, uint16_t currentTime)
{
	RETURN_IF_TRUE(((h == NULL) || (keys == NULL)  || (hit_mask == NULL)), -EINVAL);

	if(lookup_mask == 0){
		*hit_mask=0;
		return 0;
	}

	__rte_hash_hvariant_lookup_bulk(h, keys, lookup_mask,  hit_mask, updated_mask, data,currentTime, newExpirationTime, 1);
	return 0;
}





void H(rte_hash,check_integrity)(struct rte_hash_hvariant *h, uint16_t currentTime){
	RETURN_IF_TRUE((h == NULL) , -EINVAL);
	struct rte_hash_hvariant_key *next_key;
	uint32_t i,j;

	int incorrect_bucket = 0;
	int incorrect_bloom = 0;
	int incorrect_horton = 0;
	int incorrect_hash = 0;
	int in_primary = 0;
	int in_secondary = 0;
	int total = 0;
	for(i = 0; i < h->num_buckets ; i++){
		//printf("Bucket %d %x %x\n",i,i,h->buckets[i].self_index);
		for(j=0;j<RTE_HASH_HVARIANT_BUCKET_ENTRIES;j++){
			if(!free_or_expired(&h->buckets[i],j,currentTime)){
					/* Get position of entry in key table */
					int pos = i * RTE_HASH_HVARIANT_BUCKET_ENTRIES + j;
					next_key = &h->key_store[pos];

					/* Return key, data and remaining time */
					hash_key_t key = next_key->key;

					/* Check that current signature is coherent with position */
					if((h->buckets[i].primary_signature_high[j] & (h->bucket_bitmask >> 16))  != i >> 16){
						//printf("Incorrect bucket: %x %x\n", h->buckets[i].primary_signature_high[j],i>>16);
						incorrect_bucket++;
					}

					uint64_t sig = rte_hash_m128i(key);

					uint64_t prim_sig = primary_signature(h,&h->buckets[i],j);
					uint64_t sec_sig = h->buckets[i].secondary_signature_full[j];

#if BLOOM
					int in_primary_pos = !get_bit_in_mask(&h->buckets[i].mask_in_secondary_position,j);
#else
					int in_primary_pos = (sig & 0xffffffffULL) == prim_sig;
#endif

					if(in_primary_pos){
#if HORTON
						if(((sig &0xffffffffU) !=  prim_sig)||
							((sig >> 32) & ~h->bucket_bitmask) != (sec_sig & ~h->bucket_bitmask)){
#else
						if(sig != (prim_sig | (sec_sig << 32))){
#endif
							uint32_t * key = (uint32_t*)&next_key->key;
							printf("Expected (Prim): %x %x - Found: %x %x (key position %d)\n", (uint)(sig), (uint)(sig >> 32), (uint)prim_sig, (uint)sec_sig, pos);
							printf("  Key is %x %x %x %x\n", key[0], key[1], key[2], key[3]);
							incorrect_hash++;
						}else{
							in_primary++;
						}
					}else{
						/* Check that bloom filter is correct*/
#if BLOOM
						uint64_t bloom = bloom_mask_64(prim_sig);
						struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[sec_sig & h->bucket_bitmask];
						if((prim_bucket->bloom_moved & bloom) != bloom){
							incorrect_bloom++;
						}
#endif
#if HORTON
						struct rte_hash_hvariant_bucket * prim_bucket = &h->buckets[sec_sig & h->bucket_bitmask];
						int could_be_in_secondary = horton_get_hindex(prim_bucket,prim_sig);
						if(0 == could_be_in_secondary){
							printf("horton: %d should not be zero\n", could_be_in_secondary);
							incorrect_horton++;
						}
#endif
						/* Check that signature is coherent */
#if HORTON
						if(((sig &0xffffffffU) !=  sec_sig)||
							((sig >> 32) & ~h->bucket_bitmask) != (prim_sig & ~h->bucket_bitmask)){
#else
						if(sig != (sec_sig | (prim_sig << 32))){
#endif
							uint32_t * key = (uint32_t*)&next_key->key;
							printf("Expected (Seco): %x %x - Found: %x %x (key position %d)\n", (uint)(sig), (uint)(sig >> 32), (uint)prim_sig, (uint)sec_sig, pos);
							printf("  Key is %x %x %x %x\n", key[0], key[1], key[2], key[3]);
							incorrect_hash++;
						}else{
							in_secondary++;
						}
					}
					total++;
				}
			}
	}

	if(incorrect_hash > 0 ) printf("HASH: Incorrect hash (%d occurrences)\n", incorrect_hash);
	if(incorrect_bucket > 0 ) printf("HASH: Incorrect bucket (%d occurrences)\n", incorrect_bucket);
	if(incorrect_bloom > 0 ) printf("HASH: Incorrect bloom (%d occurrences)\n", incorrect_bloom);
	if(incorrect_horton > 0 ) printf("HASH: Incorrect horton (%d occurrences)\n", incorrect_horton);

	printf("%.2f primary, %.2f secondary\n",(double)in_primary/(double)total, (double)in_secondary/(double)total);
}

double H(rte_hash,stats_secondary)(struct rte_hash_hvariant *h, uint16_t currentTime){
	RETURN_IF_TRUE((h == NULL) , -EINVAL);

	uint32_t i,j;

	int in_secondary = 0;
	int total = 0;
	for(i = 0; i < h->num_buckets ; i++){
		//printf("Bucket %d %x %x\n",i,i,h->buckets[i].self_index);
		for(j=0;j<RTE_HASH_HVARIANT_BUCKET_ENTRIES;j++){
			if(!free_or_expired(&h->buckets[i],j,currentTime)){
#if BLOOM||HORTON
					int in_secondary_pos = get_bit_in_mask(&h->buckets[i].mask_in_secondary_position,j);
#else
					int pos = i * RTE_HASH_HVARIANT_BUCKET_ENTRIES + j;
					struct rte_hash_hvariant_key *next_key = &h->key_store[pos];

					/* Return key, data and remaining time */
					hash_key_t key = next_key->key;

					uint64_t sig = rte_hash_m128i(key);


					uint64_t sec_sig = h->buckets[i].secondary_signature_full[j];
					int in_secondary_pos = (sig & 0xffffffffULL) == sec_sig;
#endif

					if(in_secondary_pos){
							in_secondary++;
					}
					total++;
				}
			}
	}

	return ((double)in_secondary)/(double)total;
}


void
H(rte_hash,iterator_reset)(struct rte_hash_hvariant *h)
{

	RETURN_IF_TRUE((h == NULL), -EINVAL);


	h->iter_bucket_idx=0;
	uint32_t i;
	for(i=0;i<h->num_buckets/ITERATOR_GROUP/64;i++){
		h->iter_group_bucket_mask[i]=0ULL;
#ifdef FAST_RESET_ITERATOR
		h->reset_group_bucket_mask[i]=0xffffffffffffffffULL;
#endif
	}
#ifndef FAST_RESET_ITERATOR
	for(i=0;i<h->num_buckets;i++){
			h->buckets[i].mask_iterated_over = 0;
	}
#endif
}


#ifdef FAST_ITERATOR
int32_t
H(rte_hash,iterate)(struct rte_hash_hvariant *h, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime)
{
	struct rte_hash_hvariant_key *next_key;
	uint32_t max_iteration = 2048; // Limit the maximum pause for iterating ( 2048 buckets iterated ~= time to receive 32 packets)

	RETURN_IF_TRUE((h == NULL) , -EINVAL);

	for(uint32_t current_groupgroup = h->iter_bucket_idx/ITERATOR_GROUP/64;current_groupgroup < h->num_buckets / ITERATOR_GROUP/64; current_groupgroup++){
		if(h->iter_group_bucket_mask[current_groupgroup] == 0xffffffffffffffffULL){
			// Group Group has been fully iterated, continue to next Group group
			h->iter_bucket_idx = (current_groupgroup+1)*ITERATOR_GROUP*64;
			continue;
		}else{
			for(uint32_t current_group = current_groupgroup*64;current_group < (current_groupgroup+1)*64;current_group++){
				if(get_bit_in_largemask(h->iter_group_bucket_mask, current_group)){
					// Group has been fully iterated, continue to next group
					h->iter_bucket_idx = (current_group+1)*ITERATOR_GROUP;
					continue;
				}else{
					// First reset iterator mask in each bucket of the group if needed
					if(get_bit_in_largemask(h->reset_group_bucket_mask, current_group)){
						reset_iterator_group(h, current_group);
					}

					// Iterate over all buckets till the end of the group
					for(;h->iter_bucket_idx < (current_group+1)*ITERATOR_GROUP ;h->iter_bucket_idx++){
						if(0 == max_iteration--){
							return -EBUSY;
						}
						uint32_t i = h->iter_bucket_idx;

						// If all entries in bucket have been iterated, skip to next bucket;
						if(h->buckets[i].mask_iterated_over == 0xff) continue;

						// Otherwise, update the free bit mask (to ensure that expired entries are definitively expired)
						h->buckets[i].mask_busy = ~free_or_expired_maskpos(&h->buckets[i], currentTime);

						// Iterate over entries
						for(uint32_t j=0;j<RTE_HASH_HVARIANT_BUCKET_ENTRIES;j++){
							if(! get_bit_in_mask(&h->buckets[i].mask_iterated_over,j)){
								set_bit_in_mask(&h->buckets[i].mask_iterated_over,j);
								if(!free_or_expired(&h->buckets[i],j,currentTime)){
									/* Get position of entry in key table */
									int pos = i * RTE_HASH_HVARIANT_BUCKET_ENTRIES + j;
									next_key = &h->key_store[pos];

									/* Return key, data and remaining time */
									*key = next_key->key;
									*data = next_key->data;
			#if TIMER
									*remaining_time = time_diff(h->buckets[i].expire_date_timeunit[j],currentTime);
			#else
									*remaining_time = 0;
			#endif

									return 0;
								}
							}
						}
					}
					set_bit_in_largemask(h->iter_group_bucket_mask, current_group);
				}
			}
		}
	}
	return -ENOENT;
}
#else
int32_t
H(rte_hash,iterate)(struct rte_hash_hvariant *h, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime)
{
	struct rte_hash_hvariant_key *next_key;
	uint32_t max_iteration = 2048; // Limit the maximum pause for iterating ( 2048 buckets iterated ~= time to receive 32 packets)

	RETURN_IF_TRUE((h == NULL) , -EINVAL);

	for(;h->iter_bucket_idx < h->num_buckets ;h->iter_bucket_idx++){
			if(0 == max_iteration--){
				return -EBUSY;
			}
			uint32_t i = h->iter_bucket_idx;

			// If all entries in bucket have been iterated, skip to next bucket;
			if(h->buckets[i].mask_iterated_over == 0xff) continue;

		    // Otherwise, update the free bit mask (to ensure that expired entries are definitively expired)
			h->buckets[i].mask_busy = ~free_or_expired_maskpos(&h->buckets[i], currentTime);

			// Iterate over entries
			for(uint32_t j=0;j<RTE_HASH_HVARIANT_BUCKET_ENTRIES;j++){
				if(! get_bit_in_mask(&h->buckets[i].mask_iterated_over,j)){
					set_bit_in_mask(&h->buckets[i].mask_iterated_over,j);
					if(!free_or_expired(&h->buckets[i],j,currentTime)){
						/* Get position of entry in key table */
						int pos = i * RTE_HASH_HVARIANT_BUCKET_ENTRIES + j;
						next_key = &h->key_store[pos];

						/* Return key, data and remaining time */
						*key = next_key->key;
						*data = next_key->data;
#if TIMER
						*remaining_time = time_diff(h->buckets[i].expire_date_timeunit[j],currentTime);
#else
						*remaining_time = 0;
#endif

						return 0;
					}
				}
			}
	}
	return -ENOENT;

}
#endif


int32_t
H(rte_hash,unsafe_iterate)(struct rte_hash_hvariant *h, uint64_t * pos, hash_key_t *key, hash_data_t *data, uint16_t * remaining_time, uint16_t currentTime)
{
	struct rte_hash_hvariant_key *next_key;

	RETURN_IF_TRUE((h == NULL) , -EINVAL);

	for(; (*pos) < h->num_buckets*RTE_HASH_HVARIANT_BUCKET_ENTRIES ; (*pos)++){
			uint32_t i = (*pos)/RTE_HASH_HVARIANT_BUCKET_ENTRIES;
			uint32_t j = (*pos) - i*RTE_HASH_HVARIANT_BUCKET_ENTRIES;

			if(!free_or_expired(&h->buckets[i],j,currentTime)){
				/* Get position of entry in key table */
				int p = i * RTE_HASH_HVARIANT_BUCKET_ENTRIES + j;
				next_key = &h->key_store[p];

				/* Return key, data and remaining time */
				*key = next_key->key;
				*data = next_key->data;
#if TIMER
				*remaining_time = time_diff(h->buckets[i].expire_date_timeunit[j],currentTime);
#else
				*remaining_time = 0;
#endif
				(*pos)++;
				return 0;
			}
	}
	return -ENOENT;

}



#define MAX_DIST_MOVED 16u

void H(rte_hash,print_stats)(struct rte_hash_hvariant *h, uint16_t currentTime){
	unsigned i,j;
	int count_bucket_per_occupation[RTE_HASH_HVARIANT_BUCKET_ENTRIES+1];
	memset(count_bucket_per_occupation,0,(RTE_HASH_HVARIANT_BUCKET_ENTRIES+1)*sizeof(int));
	unsigned entriesOccupied = 0;
	__rte_unused int count_bucket_per_movedsecondarycount[MAX_DIST_MOVED]= {0};

	for( i = 0 ; i < h->num_buckets; i++){
		//if(h->buckets[i].bloom_moved != 0 && h->buckets[i].bloom_moved != 0xffff ) printf("B: %lx\n",h->buckets[i].bloom_moved);
		//if(h->buckets[i].mask_in_secondary_position) printf("MSP: %x\n",(uint32_t)h->buckets[i].mask_in_secondary_position);
		int occupied=0;
		for(j = 0 ; j < RTE_HASH_HVARIANT_BUCKET_ENTRIES; j++){
			if(!free_or_expired(&h->buckets[i],j,currentTime)){
				occupied++;
				entriesOccupied++;
			}
		}
		count_bucket_per_occupation[occupied]++;
#if BLOOM
		count_bucket_per_movedsecondarycount[RTE_MIN(MAX_DIST_MOVED - 1,h->buckets[i].count_moved_to_secondary)]++;
#elif HORTON
		uint32_t count_horton=0;
		for(j=0;j< RTE_HASH_HORTON_REMAP_ENTRIES;j++){
			count_horton += (0 != horton_get_hindex_fromtag(&h->buckets[i],j));
		}
		count_bucket_per_movedsecondarycount[RTE_MIN(MAX_DIST_MOVED - 1,count_horton)]++;
#endif
	}
	printf("overall : %f full (%u/%u)\n",((float)entriesOccupied)/(float)h->entries,entriesOccupied,h->entries);
	for(i = 0; i <= RTE_HASH_HVARIANT_BUCKET_ENTRIES; i++){
		printf("%u/%u: %.3f,  ", i, RTE_HASH_HVARIANT_BUCKET_ENTRIES, ((float)count_bucket_per_occupation[i])/(float)h->num_buckets);
	}
	printf("\n");

#if BLOOM||HORTON
#if BLOOM
	printf("Bloom filter state (distribution of values in counter moved to secondary)\n");
	float k=2;
	float m=64;
#elif HORTON
	printf("Horton filter state (distribution of filling)\n");
	float k=1;
	float m=21;
#endif
	float total=0.0;
	for(i=0; i<MAX_DIST_MOVED ;i++){
		float proba = ((float)count_bucket_per_movedsecondarycount[i])/(float)h->num_buckets;
		float fpr = pow(1.0 - exp(- k*i/m),k);
		total += i == 0 ?  0.0 : fpr * proba;
		printf("%d : %f\n", i, proba);
	}
	printf("FPR: %f\n",total);
#endif



}

uint32_t H(rte_hash,size)(struct rte_hash_hvariant *h, uint16_t currentTime){
	unsigned i,j;
	uint32_t size=0;

	for( i = 0 ; i < h->num_buckets; i++){
		for(j = 0 ; j < RTE_HASH_HVARIANT_BUCKET_ENTRIES; j++){
			if(!free_or_expired(&h->buckets[i],j,currentTime)){
				size++;
			}
		}
	}

	return size;
}

uint32_t H(rte_hash,capacity)(struct rte_hash_hvariant *h){
	return h->num_buckets*RTE_HASH_HVARIANT_BUCKET_ENTRIES;
}

int H(rte_hash,slots_per_bucket)(void){
	return RTE_HASH_HVARIANT_BUCKET_ENTRIES;
}
