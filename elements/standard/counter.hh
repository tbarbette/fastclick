// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_COUNTER_HH
#define CLICK_COUNTER_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
#include <click/llrpc.h>
#include <click/sync.hh>
#include <click/multithread.hh>
CLICK_DECLS
class HandlerCall;

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


class CounterMP : public CounterBase { public:

	CounterMP() CLICK_COLD;
    ~CounterMP() CLICK_COLD;

    const char *class_name() const		{ return "CounterMP"; }
    const char *processing() const		{ return AGNOSTIC; }
    const char *port_count() const		{ return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterMP", name) == 0)
            return (CounterMP *)this;
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
    rXwlock _atomic_lock CLICK_CACHE_ALIGN;
    per_thread<stats> _stats CLICK_CACHE_ALIGN;
};

class CounterRxWMP : public CounterMP { public:

    CounterRxWMP() CLICK_COLD;
    ~CounterRxWMP() CLICK_COLD;

    const char *class_name() const      { return "CounterRxWMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }
};
/*
class CounterRCUMP : public CounterBase { public:

    CounterRCUMP() CLICK_COLD;
    ~CounterRCUMP() CLICK_COLD;

    const char *class_name() const      { return "CounterRCUMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterRCUMP", name) == 0)
            return (CounterRCUMP *)this;
        else
            return CounterBase::cast(name);
    }

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        int flags;
        const per_thread<stats>& stats = _stats.read_begin(flags);
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,stats,_count);
        _stats.read_end(flags);
        return sum;
    }

    counter_int_type byte_count() {
        int flags;
        const per_thread<stats>& stats = _stats.read_begin(flags);
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,stats,_byte_count);
        _stats.read_end(flags);
        return sum;
    }

    stats atomic_read() {
        int flags;
        const per_thread<stats>& stats = _stats.read_begin(flags);
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        for (unsigned i = 0; i < stats.weight(); i++) { \
            count += stats.get_value(i)._count;
            byte_count += stats.get_value(i)._byte_count;
        }
        _stats.read_end(flags);
        return {count,byte_count};
    }


protected:
    fast_rcu<per_thread<stats> > _stats;
};
*/
class CounterRCU : public CounterBase { public:

    CounterRCU() CLICK_COLD;
    ~CounterRCU() CLICK_COLD;

    const char *class_name() const      { return "CounterRCU"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterRCU", name) == 0)
            return (CounterRCU *)this;
        else
            return CounterBase::cast(name);
    }

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        int flags;
        const stats& stats = _stats.read_begin(flags);
        counter_int_type sum = stats._count;
        _stats.read_end(flags);
        return sum;
    }

    counter_int_type byte_count() {
        int flags;
        const stats& stats = _stats.read_begin(flags);
        counter_int_type sum = stats._byte_count;
        _stats.read_end(flags);
        return sum;
    }

    stats read() {
        int flags;
        const stats& s = _stats.read_begin(flags);
        stats copy = s;
        _stats.read_end(flags);
        return copy;
    }

    stats atomic_read() {
        int flags;
        const stats& s = _stats.read_begin(flags);
        stats copy = s;
        _stats.read_end(flags);
        return copy;
    }


    void add(stats s) override {
        int flags;
        stats& stats = _stats.write_begin(flags);
        stats._count += s._count;
        stats._byte_count += s._byte_count;
        _stats.write_commit(flags);
    }

    void atomic_add(stats s) override {
        int flags;
        stats& stats = _stats.write_begin(flags);
        stats._count += s._count;
        stats._byte_count += s._byte_count;
        _stats.write_commit(flags);
    }

protected:
    fast_rcu<stats> _stats;
};


class CounterAtomic : public CounterBase { public:

    CounterAtomic() CLICK_COLD;
    ~CounterAtomic() CLICK_COLD;

    const char *class_name() const      { return "CounterAtomic"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterAtomic", name) == 0)
            return (CounterAtomic *)this;
        else
            return CounterBase::cast(name);
    }

    //Only per-counter atomic
    int can_atomic() { return 1; } CLICK_COLD;

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

    stats read() override {
        return {_count, _byte_count};
    }

    stats atomic_read() override {  //This is NOT atomic between the two fields
        return {_count, _byte_count};
    }


    void add(stats s) override {
        _count += s._count;
        _byte_count += s._byte_count;
    }

    void atomic_add(stats s) override {
        _count += s._count;
        _byte_count += s._byte_count;
    }

protected:
    counter_atomic_int_type _count;
    counter_atomic_int_type _byte_count;
};

class CounterLock : public CounterBase { public:

    CounterLock() CLICK_COLD;
    ~CounterLock() CLICK_COLD;

    const char *class_name() const      { return "CounterLock"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterLock", name) == 0)
            return (CounterLock *)this;
        else
            return CounterBase::cast(name);
    }

    int can_atomic() { return 2; } CLICK_COLD;

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
        stats s;
        _lock.acquire();
        s = {_count, _byte_count};
        click_read_fence();
        _lock.release();
        return s;
    }

    stats atomic_read() {
        stats s;
        _lock.acquire();
        s = {_count, _byte_count};
        click_read_fence();
        _lock.release();
        return s;
    }

    void add(stats s) override {
        _lock.acquire();
        _count += s._count;
        _byte_count += s._byte_count;
        _lock.release();
    }

    void atomic_add(stats s) override {
        _lock.acquire();
        _count += s._count;
        _byte_count += s._byte_count;
        _lock.release();
    }

protected:
    volatile counter_int_type _count;
    volatile counter_int_type _byte_count;
    Spinlock _lock;
};

class CounterRW : public CounterBase { public:

    CounterRW() CLICK_COLD;
    ~CounterRW() CLICK_COLD;

    const char *class_name() const      { return "CounterRW"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterRW", name) == 0)
            return (CounterRW *)this;
        else
            return CounterBase::cast(name);
    }

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        _lock.read_begin();
        counter_int_type v = (*_lock)._count;
        _lock.read_end();
        return v;
    }

    counter_int_type byte_count() {
        _lock.read_begin();
        counter_int_type v = (*_lock)._byte_count;
        _lock.read_end();
        return v;
    }

    stats read() {
        stats s;
        _lock.read_begin();
        s = (*_lock);
        _lock.read_end();
        return s;
    }

    stats atomic_read() {
        stats s;
        _lock.read_begin();
        s = (*_lock);
        _lock.read_end();
        return s;
    }

    void add(stats s) override {
        _lock.write_begin();
        _lock->_count += s._count;
        _lock->_byte_count += s._byte_count;
        _lock.write_end();
    }

    void atomic_add(stats s) override {
        _lock.write_begin();
        _lock->_count += s._count;
        _lock->_byte_count += s._byte_count;
        _lock.write_end();
    }

protected:
    __rwlock<stats> _lock;
};

class CounterPRW : public CounterBase { public:

    CounterPRW() CLICK_COLD;
    ~CounterPRW() CLICK_COLD;

    const char *class_name() const      { return "CounterPRW"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterPRW", name) == 0)
            return (CounterPRW *)this;
        else
            return CounterBase::cast(name);
    }

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() {
        _lock.acquire_read();
        counter_int_type v = _s._count;
        _lock.release_read();
        return v;
    }

    counter_int_type byte_count() {
        _lock.acquire_read();
        counter_int_type v = _s._byte_count;
        _lock.release_read();
        return v;
    }

    stats read() {
        stats s;
        _lock.acquire_read();
        s = (_s);
        _lock.release_read();
        return s;
    }

    stats atomic_read() {
        stats s;
        _lock.acquire_read();
        s = (_s);
        _lock.release_read();
        return s;
    }

    void add(stats s) override {
        _lock.acquire_write();
        _s._count += s._count;
        _s._byte_count += s._byte_count;
        _lock.release_write();
    }

    void atomic_add(stats s) override {
        _lock.acquire_write();
        _s._count += s._count;
        _s._byte_count += s._byte_count;
        _lock.release_write();
    }

protected:
    stats _s;
    ReadWriteLock _lock;
};

CLICK_ENDDECLS
#endif
