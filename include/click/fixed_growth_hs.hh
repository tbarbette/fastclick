#ifndef FIXED_ROBIN_GROWTH_POLICY_H
#define FIXED_ROBIN_GROWTH_POLICY_H 


#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ratio>
#include <stdexcept>

// Prints

#include <click/glue.hh>

namespace tsl {
/**
 * Maintain a fixed size for the table 
 */
class fixed_size_policy{
public:
    /**
     * Called on the hash table creation and on rehash. The number of buckets for the table is passed in parameter.
     * This number is a minimum, the policy may update this value with a higher value if needed (but not lower).
     *
     * If 0 is given, min_bucket_count_in_out must still be 0 after the policy creation and
     * bucket_for_hash must always return 0 in this case.
     */
    explicit fixed_size_policy(std::size_t& min_bucket_count_in_out) {
        if(min_bucket_count_in_out > max_bucket_count()) {
	    m_mask = 0;
	}
        
        if(min_bucket_count_in_out > 0) {
            min_bucket_count_in_out = round_up_to_power_of_two(min_bucket_count_in_out);
	    //click_chatter("m_mask set to %lu, min_bycket_count_in_out set to %lu",
	    //m_mask, min_bucket_count_in_out);
            m_mask = min_bucket_count_in_out - 1;
        }
        else {
            m_mask = 0;
        }
	// click_chatter("m_mask is %lu\n", m_mask);
    }
    
    /**
     * Return the bucket [0, bucket_count()) to which the hash belongs. 
     * If bucket_count() is 0, it must always return 0.
     */
    std::size_t bucket_for_hash(std::size_t hash) const noexcept {
        return hash & m_mask;
    }
    
    /**
     * Return the number of buckets that should be used on next growth.
     */
    std::size_t next_bucket_count() const {
	//click_chatter("next_bucket_count is %lu", m_mask+1);
	return m_mask+1;
        // if((m_mask + 1) > max_bucket_count() / GrowthFactor) {
        //     TSL_RH_THROW_OR_TERMINATE(std::length_error, "The hash table exceeds its maximum size.");
        // }
        
        //return (m_mask + 1) * GrowthFactor;
    }
    
    /**
     * Return the maximum number of buckets supported by the policy.
     */
    std::size_t max_bucket_count() const {
//	click_chatter("max_bucket_count is %i", m_mask+1);
        // Largest power of two.
	//return m_mask +1;
	// click_chatter("next_bucket_count is %lu", (std::numeric_limits<std::size_t>::max() / 2) + 1);
        return (std::numeric_limits<std::size_t>::max() / 2) + 1;
    }
    
    /**
     * Reset the growth policy as if it was created with a bucket count of 0.
     * After a clear, the policy must always return 0 when bucket_for_hash is called.
     */
    void clear() noexcept {
        m_mask = 0;
    }
    
private:
    static std::size_t round_up_to_power_of_two(std::size_t value) {
        if(is_power_of_two(value)) {
            return value;
        }
        
        if(value == 0) {
            return 1;
        }
            
        --value;
        for(std::size_t i = 1; i < sizeof(std::size_t) * CHAR_BIT; i *= 2) {
            value |= value >> i;
        }
        
        return value + 1;
    }
    
    static constexpr bool is_power_of_two(std::size_t value) {
        return value != 0 && (value & (value - 1)) == 0;
    }
    
protected:
    //static_assert(is_power_of_two(GrowthFactor) && GrowthFactor >= 2, "GrowthFactor must be a power of two >= 2.");
    
    std::size_t m_mask;
};


/**
 * Grow the hash table by GrowthFactor::num / GrowthFactor::den and use a modulo
 * to map a hash to a bucket. Slower but it can be useful if you want a slower
 * growth.
 */
class fixed_mod_growth_policy {
 public:
  explicit fixed_mod_growth_policy(std::size_t& min_bucket_count_in_out) {
    if (min_bucket_count_in_out > max_bucket_count()) {
	click_chatter("The hash table exceeds its maximum size.");
    }

    if (min_bucket_count_in_out > 0) {
      m_mod = min_bucket_count_in_out;
    } else {
      m_mod = 1;
    }
  }

  std::size_t bucket_for_hash(std::size_t hash) const noexcept {
    return hash % m_mod;
  }

  std::size_t next_bucket_count() const {
    return m_mod;
    // if (m_mod == max_bucket_count()) {
    //                             click_chatter("The hash table exceeds its maximum size.");
    // }

    // const double next_bucket_count =
    //     std::ceil(double(m_mod) * REHASH_SIZE_MULTIPLICATION_FACTOR);
    // if (!std::isnormal(next_bucket_count)) {
    //   click_chatter(
    //                             "The hash table exceeds its maximum size.");
    // }

    // if (next_bucket_count > double(max_bucket_count())) {
    //   return max_bucket_count();
    // } else {
    //   return std::size_t(next_bucket_count);
    // }
  }

  std::size_t max_bucket_count() const { return MAX_BUCKET_COUNT; }

  void clear() noexcept { m_mod = 1; }

 private:
  static constexpr double REHASH_SIZE_MULTIPLICATION_FACTOR =
      1.0;
  static const std::size_t MAX_BUCKET_COUNT =
      std::size_t(std::numeric_limits<std::size_t>::max());

  std::size_t m_mod;
};


} // tsl


#endif
