// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TSCTIMESTAMP_HH
#define CLICK_TSCTIMESTAMP_HH
#include <click/glue.hh>
#include <click/timestamp.hh>

CLICK_DECLS

class TSCTimestamp {
    public:
    typedef uninitialized_type uninitialized_t;
    typedef int64_t cycles_t;

    TSCTimestamp() {
        val = 0;
    }

    explicit TSCTimestamp(cycles_t v) {
        val = v;
    }

    static inline click_cycles_t cycles_hz_warp() {
#if TIMESTAMP_WARPABLE
        if (Timestamp::warp_class() == Timestamp::warp_simulation) {
            return 3000000000;
        }
#endif
        return cycles_hz();
    }

    /** @brief Construct an uninitialized timestamp. */
    inline TSCTimestamp(const uninitialized_t &unused) {
        (void) unused;
    }

    static inline TSCTimestamp now() {
#if TIMESTAMP_WARPABLE
        if (Timestamp::warp_class() == Timestamp::warp_simulation) {
            return TSCTimestamp((cycles_t)Timestamp::now_steady().nsecval() * 3);
        }
#endif
        return TSCTimestamp(click_get_cycles());
    }

    static inline TSCTimestamp now_steady() {
        return now();
    }

    friend inline bool operator>(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline bool operator<(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline bool operator>=(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline bool operator<=(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline bool operator==(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline bool operator!=(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline TSCTimestamp operator+(const TSCTimestamp &a, const TSCTimestamp &b);
    friend inline TSCTimestamp operator-(const TSCTimestamp &a, const TSCTimestamp &b);


    inline String unparse() const {
        double t = (double)(val) / (double)cycles_hz_warp();
        return String(t);
    }


    /**
     * @brief returns the total number of msecs represented by this timestamp
     */
    inline int64_t msecval() {
        return val / (cycles_hz_warp() / 1000);
    }

    /**
     * @brief returns the total number of usecs represented by this timestamp
     */
    inline int64_t usecval() {
        return val / (cycles_hz_warp() / 1000000);
    }

    /**
     * @brief returns the total number of nsecs represented by this timestamp
     */
    inline double nsecval() {
        return (double)val / ((double)cycles_hz_warp() / 1000000000.0d);
    }

    inline int64_t tsc_val() {
        return val;
    }

    static inline TSCTimestamp make_msec(unsigned n) {
        return TSCTimestamp((n * cycles_hz_warp()) / 1000);
    }

    static inline TSCTimestamp make_usec(unsigned n) {
        return TSCTimestamp((n * cycles_hz_warp()) / 1000000);
    }

    inline operator Timestamp() {
        return Timestamp::make_nsec((double)val / ((double)cycles_hz_warp() / 1000000000.0));
    }


    private:
    cycles_t val;
};

/** @relates TSCTimestamp
    @brief Compare two timestamps for difference.

    Returns true iff the two operands have different value. */
inline bool
operator!=(const TSCTimestamp &a, const TSCTimestamp &b)
{
    return a.val != b.val;
}


/** @relates TSCTimestamp
    @brief Compare two timestamps for equality.

    Returns true iff the two operands have the same value. */
inline bool
operator==(const TSCTimestamp &a, const TSCTimestamp &b)
{
    return a.val == b.val;
}

inline bool operator<(const TSCTimestamp &a, const TSCTimestamp &b) {
    return a.val < b.val;
}

inline bool operator<=(const TSCTimestamp &a, const TSCTimestamp &b) {
    return a.val <= b.val;
}

inline bool operator>(const TSCTimestamp &a, const TSCTimestamp &b) {
    return a.val > b.val;
}

inline bool operator>=(const TSCTimestamp &a, const TSCTimestamp &b) {
    return a.val >= b.val;
}


inline TSCTimestamp operator+(const TSCTimestamp &a, const TSCTimestamp &b) {
    return TSCTimestamp(a.val + b.val);
}

inline TSCTimestamp operator-(const TSCTimestamp &a, const TSCTimestamp &b) {
    return TSCTimestamp(a.val - b.val);
}

CLICK_ENDDECLS

#endif
