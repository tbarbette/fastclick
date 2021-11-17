#ifndef CLICK_AVERAGECOUNTER_HH
#define CLICK_AVERAGECOUNTER_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
#include <click/atomic.hh>
#include <click/timer.hh>
CLICK_DECLS

/*
 * =c
 * AverageCounter([IGNORE])
 * =s counters
 * measures historical packet count and rate
 * =d
 *
 * Passes packets unchanged from its input to its
 * output, maintaining statistics information about
 * packet count and packet rate using a strict average.
 *
 * The rate covers only the time between the first and
 * most recent packets.
 *
 * IGNORE, by default, is 0. If it is greater than 0,
 * the first IGNORE number of seconds are ignored in
 * the count.
 *
 * =h count read-only
 * Returns the number of packets that have passed through since the last reset.
 *
 * =h byte_count read-only
 * Returns the number of packets that have passed through since the last reset.
 *
 * =h rate read-only
 * Returns packet arrival rate.
 *
 * =h byte_rate read-only
 * Returns packet arrival rate in bytes per second.  (Beware overflow!)
 *
 * =h reset write-only
 * Resets the count and rate to zero.
 */

template <typename Stats>
class AverageCounterBase : public BatchElement { public:

    AverageCounterBase() CLICK_COLD;

    const char *port_count() const override		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    inline uint64_t count() const           { return _stats.count(); }
    inline uint64_t byte_count() const      { return _stats.byte_count(); }
    inline uint64_t first() const			{ return _stats.first(); }
    inline uint64_t last() const			{ return _stats.last(); }
    inline uint64_t ignore() const			{ return _ignore; }
    inline void reset(bool with_time=false);

#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *batch);
#endif
    Packet *simple_action(Packet *);


    bool _link_fcs;
  private:
    static String averagecounter_read_rate_handler(Element *e, void *thunk);
    Stats _stats;
    uint64_t _ignore;
    uint32_t _threshold;
    uint32_t _max;
};


template <typename T>
struct AverageCounterStats {
    AverageCounterStats() {
        reset();
    }

    T _count;
    T _byte_count;
    T _first;
    T _last;

    inline void add_count(uint64_t c,uint64_t b) {
        _count += c;
        _byte_count += b;
    }

    inline void reset()
    {
      _count = 0;
      _byte_count = 0;
      _first = 0;
      _last = 0;
    }

    inline uint64_t count() const { return _count; }
    inline uint64_t byte_count() const  {   return _byte_count; };
    inline uint64_t first() const { return _first; }
    inline uint64_t my_first() const { return _first; }
    inline void set_first(uint64_t first){ _first = first; }
    inline uint64_t last() const { return _last; }
    inline void set_last(uint64_t last){ _last = last; }

};

template <typename Stats>
inline void AverageCounterBase<Stats>::reset(bool with_time)	{
    _stats.reset();
    if (with_time) {
        click_jiffies_t jpart = click_jiffies();
        if (_stats.my_first() == 0)
            _stats.set_first(jpart);
    }
};


class AverageCounter : public AverageCounterBase<AverageCounterStats<uint64_t> > { public:
    AverageCounter() CLICK_COLD;

    const char *class_name() const override		{ return "AverageCounter"; }
};

class AverageCounterMP : public AverageCounterBase<AverageCounterStats<atomic_uint64_t> > { public:
    AverageCounterMP() CLICK_COLD;

    const char *class_name() const override		{ return "AverageCounterMP"; }
};


struct AverageCounterStatsIMP {
    struct Count {
        uint64_t count;
        uint64_t byte_count;
        uint64_t first;
        uint64_t last;
    };

    per_thread<Count> _counts;

    inline void add_count(uint64_t c,uint64_t b) {
        _counts->count += c;
        _counts->byte_count += b;
    }

    inline void reset()
    {
        for (unsigned i =0; i < _counts.weight(); i++) {
            _counts.get_value(i).count = 0;
            _counts.get_value(i).byte_count = 0;
            _counts.get_value(i).first = 0;
            _counts.get_value(i).last = 0;
        }
    }

    inline uint64_t count() const { PER_THREAD_MEMBER_SUM(uint64_t,total,_counts,count);return total; }
    inline uint64_t byte_count() const  { PER_THREAD_MEMBER_SUM(uint64_t,total,_counts,byte_count);return total; };
    inline uint64_t first() const {
        uint64_t min = UINT64_MAX;
        for (unsigned i =0; i < _counts.weight(); i++) {
            if (_counts.get_value(i).first != 0 && _counts.get_value(i).first < min)
                min = _counts.get_value(i).first;
        }
        return min;
    }
    inline uint64_t last() const {
        uint64_t max = 0;
        for (unsigned i =0; i < _counts.weight(); i++) {
            if (_counts.get_value(i).last != 0 && _counts.get_value(i).last > max)
                max = _counts.get_value(i).last;
        }
        return max;
    }

    inline uint64_t my_first() const { return _counts->first; }
    inline void set_first(uint64_t first){ _counts->first = first; }
    inline void set_last(uint64_t last){ _counts->last = last; }
};

class AverageCounterIMP : public AverageCounterBase<AverageCounterStatsIMP> { public:
    AverageCounterIMP() CLICK_COLD;

    const char *class_name() const override		{ return "AverageCounterIMP"; }
};



CLICK_ENDDECLS
#endif
