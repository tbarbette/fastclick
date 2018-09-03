// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_COUNTER_HH
#define CLICK_COUNTER_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
#include <click/llrpc.h>
#include <click/sync.hh>
#include <click/handlercall.hh>

CLICK_DECLS

#ifdef HAVE_INT64_TYPES
#define counter_int_type uint64_t
#define counter_atomic_int_type atomic_uint64_t
#else
#define counter_int_type uint32_t
#define counter_atomic_int_type atomic_uint32_t
#endif

/*
=c

Counter([I<keywords COUNT_CALL, BYTE_COUNT_CALL>])

=s counters

measures packet count and rate

=d

Passes packets unchanged from its input to its output, maintaining statistics
information about packet count and packet rate.

Keyword arguments are:

=over 8

=item COUNT_CALL

Argument is `I<N> I<HANDLER> [I<VALUE>]'. When the packet count reaches I<N>,
call the write handler I<HANDLER> with value I<VALUE> before emitting the
packet.

=item BYTE_COUNT_CALL

Argument is `I<N> I<HANDLER> [I<VALUE>]'. When the byte count reaches or
exceeds I<N>, call the write handler I<HANDLER> with value I<VALUE> before
emitting the packet.

=item BATCH_PRECISE

If true, will count packets one by one and hence call the handlers when the
count is exactly the limit. The default is to count the whole batch at once
and call the handlers if the limit has been reached.

=back

=h count read-only

Returns the number of packets that have passed through since the last reset.

=h byte_count read-only

Returns the number of bytes that have passed through since the last reset.

=h rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in packets per second.

=h bit_rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in bits per second.

=h byte_rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in bytes per second.

=h reset_counts write-only

Resets the counts and rates to zero.

=h reset write-only

Same as 'reset_counts'.

=h count_call write-only

Writes a new COUNT_CALL argument. The handler can be omitted.

=h byte_count_call write-only

Writes a new BYTE_COUNT_CALL argument. The handler can be omitted.

=h CLICK_LLRPC_GET_RATE llrpc

Argument is a pointer to an integer that must be 0.  Returns the recent
arrival rate (measured by exponential weighted moving average) in
packets per second.

=h CLICK_LLRPC_GET_COUNT llrpc

Argument is a pointer to an integer that must be 0 (packet count) or 1 (byte
count). Returns the current packet or byte count.

=h CLICK_LLRPC_GET_COUNTS llrpc

Argument is a pointer to a click_llrpc_counts_st structure (see
<click/llrpc.h>). The C<keys> components must be 0 (packet count) or 1 (byte
count). Stores the corresponding counts in the corresponding C<values>
components.

*/

/**
 * Ancestor of all types of counter so external elements can access count and byte_count
 * independently of the storage type
 */

class CounterBase : public BatchElement { public:

	CounterBase() CLICK_COLD;
    ~CounterBase() CLICK_COLD;

    virtual counter_int_type count() = 0;
    virtual counter_int_type byte_count() = 0;
    struct stats {
        counter_int_type _count;
        counter_int_type _byte_count;
    };

    /**
     * Read state in a non-atomic way, as fast as possible. Value of the ATOMIC
     *   parameter does not change this. Avoid locks, etc, just give a plausible
     *   value.
     */
    virtual stats read() = 0;
    /**
     * Atomic read. Atomicity level depends on the ATOMIC parameter.
     */
    virtual stats atomic_read() = 0;

    /**
     * Add something to the counter, allowing stale and inconsistent data but
     *   still thread safe (the final total must be good)
     */
    virtual void add(stats) = 0;

    /**
     * Add in an atomic way.
     */
    virtual void atomic_add(stats) = 0;

    void* cast(const char *name);
    bool do_mt_safe_check(ErrorHandler* errh);

    virtual int can_atomic() { return 0; } CLICK_COLD;
    virtual void reset() {
        if (likely(_simple))
            return;
        _count_triggered = _byte_triggered = false;
    }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    inline void check_handlers(counter_int_type count, counter_int_type byte_count);
    void add_handlers() CLICK_COLD;
    int llrpc(unsigned, void *);

  protected:

#ifdef HAVE_INT64_TYPES
    // Reduce bits of fraction for byte rate to avoid overflow
    typedef RateEWMAX<RateEWMAXParameters<4, 10, uint64_t, int64_t> > rate_t;
    typedef RateEWMAX<RateEWMAXParameters<4, 4, uint64_t, int64_t> > byte_rate_t;
#else
    typedef RateEWMAX<RateEWMAXParameters<4, 10> > rate_t;
    typedef RateEWMAX<RateEWMAXParameters<4, 4> > byte_rate_t;
#endif

    rate_t _rate;
    byte_rate_t _byte_rate;

    counter_int_type _count_trigger;
    HandlerCall *_count_trigger_h;

    counter_int_type _byte_trigger;
    HandlerCall *_byte_trigger_h;

    bool _count_triggered : 1;
    bool _byte_triggered : 1;
    bool _batch_precise;
    unsigned _atomic;
    bool _simple;
    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;
};

class Counter : public CounterBase { public:

	Counter() CLICK_COLD;
    ~Counter() CLICK_COLD;

    const char *class_name() const		{ return "Counter"; }
    const char *processing() const		{ return AGNOSTIC; }
    const char *port_count() const		{ return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("Counter", name) == 0)
            return (Counter *)this;
        else
            return CounterBase::cast(name);
    }

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        return _count;
    }

    counter_int_type byte_count() {
        return _byte_count;
    }

    stats read() {
        return {_count, _byte_count};
    }

    stats atomic_read() {  //This is NOT atomic
        return Counter::read();
    }

    void add(stats s) override {
        _count += s._count;
        _byte_count += s._byte_count;
    }

    void atomic_add(stats s) override { //This is NOT atomic
        Counter::add(s);
    }

protected:
    counter_int_type _count;
    counter_int_type _byte_count;
};

template <typename T>
class CounterMPBase : public CounterBase { public:

	CounterMPBase() CLICK_COLD;
    ~CounterMPBase() CLICK_COLD;

    const char *class_name() const		{ return "CounterMP"; }
    const char *processing() const		{ return AGNOSTIC; }
    const char *port_count() const		{ return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterMP", name) == 0)
            return (CounterMPBase<T> *)this;
        else
            return CounterBase::cast(name);
    }
    int initialize(ErrorHandler *) CLICK_COLD;

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,_count);
        return sum;
    }

    counter_int_type byte_count() {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,_byte_count);
        return sum;
    }

    stats read() {
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        for (unsigned i = 0; i < _stats.weight(); i++) { \
            count += _stats.get_value(i)._count;
            byte_count += _stats.get_value(i)._byte_count;
        }
        return {count,byte_count};
    }

    stats atomic_read() {
        if (_atomic > 0)
            _atomic_lock.read_begin();
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        for (unsigned i = 0; i < _stats.weight(); i++) { \
            count += _stats.get_value(i)._count;
            byte_count += _stats.get_value(i)._byte_count;
        }
        if (_atomic > 0)
            _atomic_lock.read_end();
        return {count,byte_count};
    }

    void add(stats s) override {
        _stats->_count += s._count;
        _stats->_byte_count += s._byte_count;
    }

    void atomic_add(stats s) override {
        if (_atomic > 0)
            _atomic_lock.write_begin();
        _stats->_count += s._count;
        _stats->_byte_count += s._byte_count;
        if (_atomic > 0)
            _atomic_lock.write_end();
    }

protected:
    T _atomic_lock CLICK_CACHE_ALIGN;
    per_thread<stats> _stats CLICK_CACHE_ALIGN;
};

/**
 * =c
 *
 * CounterMP() Thread-safe counter duplicated per-thread
 *
 * =s counters
 *
 * =d
 *
 * Works as Counter does, but with the internal counter duplicated per-thread.
 * The value is aggregated upon read.
 *
 * See Counter for more usage informations.
 *
 * =a Counter
 */
class CounterMP : public CounterMPBase<nolock> { public:

};

inline void
CounterBase::check_handlers(counter_int_type count, counter_int_type byte_count) {
    if (count == _count_trigger && !_count_triggered) {
        _count_triggered = true;
        if (_count_trigger_h)
            (void) _count_trigger_h->call_write();
    }
    if (byte_count == _byte_trigger && !_byte_triggered) {
        _byte_triggered = true;
        if (_byte_trigger_h)
            (void) _byte_trigger_h->call_write();
    }
}

template <typename T>
CounterMPBase<T>::CounterMPBase()
{
}
template <typename T>
CounterMPBase<T>::~CounterMPBase()
{
}

template <typename T>
int
CounterMPBase<T>::initialize(ErrorHandler *errh) {
    if (CounterBase::initialize(errh) != 0)
        return -1;
    //If not in simple mode, we only allow one writer so we can sum up the total number of threads
    if (!_simple)
        _atomic_lock.set_max_writers(1);
    return 0;
}

template <typename T>
Packet*
CounterMPBase<T>::simple_action(Packet *p)
{
    if (_atomic > 0)
        _atomic_lock.write_begin();
    _stats->_count++;
    _stats->_byte_count += p->length();
    if (unlikely(!_simple))
        check_handlers(CounterMPBase<T>::count(), CounterMPBase<T>::byte_count()); //BUG : if not atomic, then handler may be called twice
    if (_atomic > 0)
        _atomic_lock.write_end();
    return p;
}

#if HAVE_BATCH
template <typename T>
PacketBatch*
CounterMPBase<T>::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
                                CounterMPBase<T>::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    if (_atomic > 0)
        _atomic_lock.write_begin();
    _stats->_count += batch->count();
    _stats->_byte_count += bc;
    if (unlikely(!_simple))
        check_handlers(CounterMPBase<T>::count(), CounterMPBase<T>::byte_count());
    if (_atomic > 0)
        _atomic_lock.write_end();
    return batch;
}
#endif

template <typename T>
void
CounterMPBase<T>::reset()
{
    if (_atomic  > 0)
        _atomic_lock.write_begin();
    for (unsigned i = 0; i < _stats.weight(); i++) { \
        _stats.get_value(i)._count = 0;
        _stats.get_value(i)._byte_count = 0;
    }
    CounterBase::reset();
    if (_atomic  > 0)
        _atomic_lock.write_end();
}



CLICK_ENDDECLS
#endif
