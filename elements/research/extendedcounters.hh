// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_EXTENDEDCOUNTERS_HH
#define CLICK_EXTENDEDCOUNTERS_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
#include <click/llrpc.h>
#include <click/sync.hh>
#include <click/multithread.hh>
#include "../standard/counter.hh"

CLICK_DECLS
class HandlerCall;

template <typename T>
class CounterRxWMPBase : public CounterMPBase<T> { public:

    CounterRxWMPBase() CLICK_COLD {
    }

    ~CounterRxWMPBase() CLICK_COLD {
    }

    const char *processing() const      { return Element::AGNOSTIC; }
    const char *port_count() const      { return Element::PORTS_1_1; }
};

/**
 *
 * =c
 *
 * CounterRxWMP()
 *
 * Counter duplicated per-thread, protected by a RxW lock
 *
 * =s research
 *
 * =d
 *
 * The RxW lock allows either multiple writers or multiple readers, so this
 * counter has the ability to give an atomic view of the aggregated state.
 *
 * The CounterRxWMPPR and CounterRxWMPW respectively prefer reads and writes,
 * that is when a read/write comes, no write/read can start, ensuring no
 * starvation.
 *
 * =a CounterRxWMPPR, CounterRXWMPPW
 *
 */
class CounterRxWMP : public CounterRxWMPBase<rXwlock> { public:

    CounterRxWMP() CLICK_COLD;
    ~CounterRxWMP() CLICK_COLD;

    const char *class_name() const      { return "CounterRxWMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }
};

class CounterRxWMPPR : public CounterRxWMPBase<rXwlockPR> { public:

    CounterRxWMPPR() CLICK_COLD;
    ~CounterRxWMPPR() CLICK_COLD;

    const char *class_name() const      { return "CounterRxWMPPR"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }
};

class CounterRxWMPPW : public CounterRxWMPBase<rXwlockPW> { public:

    CounterRxWMPPW() CLICK_COLD;
    ~CounterRxWMPPW() CLICK_COLD;

    const char *class_name() const      { return "CounterRxWMPPW"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }
};


/*
 * =c
 *
 * CounterLockMP()
 *
 * Counter duplicated per-thread, with a per-thread lock
 *
 * =s research
 */
class CounterLockMP : public CounterBase { public:

    CounterLockMP() CLICK_COLD;
    ~CounterLockMP() CLICK_COLD;

    const char *class_name() const      { return "CounterLockMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }


    int initialize(ErrorHandler *) CLICK_COLD;

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() override {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,s._count);
        return sum;
    }

    counter_int_type byte_count() override {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,s._byte_count);
        return sum;
    }

    stats read() {
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        for (unsigned i = 0; i < _stats.weight(); i++) { \
            count += _stats.get_value(i).s._count;
            byte_count += _stats.get_value(i).s._byte_count;
        }
        return {count,byte_count};
    }

    inline void acquire() {
        for (unsigned i = 0; i < _stats.weight(); i++) {
            _stats.get_value(i).lock.acquire();
        }
    }

    inline void release() {
        for (unsigned i = 0; i < _stats.weight(); i++)
            _stats.get_value(i).lock.release();
    }

    stats atomic_read() {
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        if (_atomic == 2) {
            acquire();
            for (unsigned i = 0; i < _stats.weight(); i++) {
                count += _stats.get_value(i).s._count;
                byte_count += _stats.get_value(i).s._byte_count;
            }
            release();
        } else {
            for (unsigned i = 0; i < _stats.weight(); i++) {
                _stats.get_value(i).lock.acquire();
                count += _stats.get_value(i).s._count;
                byte_count += _stats.get_value(i).s._byte_count;
                _stats.get_value(i).lock.release();
            }
        }

        return {count,byte_count};
    }

    void add(stats s) override {
        _stats->s._count += s._count;
        _stats->s._byte_count += s._byte_count;
    }

    void atomic_add(stats s) override {
        _stats->lock.acquire();
        _stats->s._count += s._count;
        _stats->s._byte_count += s._byte_count;
        _stats->lock.release();
    }

protected:
    class bucket { public:
        bucket() : s(), lock() {
        }
        stats s;
        Spinlock lock;
    };
    per_thread<bucket> _stats CLICK_CACHE_ALIGN;

};

class CounterPLockMP : public CounterLockMP { public:

    CounterPLockMP() CLICK_COLD;
    ~CounterPLockMP() CLICK_COLD;

    const char *class_name() const      { return "CounterPLockMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }
};


/*
 * =c
 *
 * CounterRWMP()
 *
 * Counter duplicated per-thread, with a global RW lock
 *
 * =s research
 *
 */
class CounterRWMP : public CounterBase { public:

    CounterRWMP() CLICK_COLD;
    ~CounterRWMP() CLICK_COLD;

    const char *class_name() const      { return "CounterRWMP"; }
    const char *processing() const      { return AGNOSTIC; }
    const char *port_count() const      { return PORTS_1_1; }


    int initialize(ErrorHandler *) CLICK_COLD;

    int can_atomic() { return 2; } CLICK_COLD;

    Packet *simple_action(Packet *);
#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch* batch);
#endif

    void reset();

    counter_int_type count() override {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,s._count);
        return sum;
    }

    counter_int_type byte_count() override {
        PER_THREAD_MEMBER_SUM(counter_int_type,sum,_stats,s._byte_count);
        return sum;
    }

    stats read() {
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        for (unsigned i = 0; i < _stats.weight(); i++) { \
            count += _stats.get_value(i).s._count;
            byte_count += _stats.get_value(i).s._byte_count;
        }
        return {count,byte_count};
    }

    inline void acquire_read() {
        for (unsigned i = 0; i < _stats.weight(); i++) {
            _stats.get_value(i).lock.read_begin();
        }
    }

    inline void release_read() {
        for (unsigned i = 0; i < _stats.weight(); i++)
            _stats.get_value(i).lock.read_end();
    }

    inline void acquire_write() {
        for (unsigned i = 0; i < _stats.weight(); i++) {
            _stats.get_value(i).lock.write_begin();
        }
    }

    inline void release_write() {
        for (unsigned i = 0; i < _stats.weight(); i++)
            _stats.get_value(i).lock.write_end();
    }
    stats atomic_read() {
        counter_int_type count = 0;
        counter_int_type byte_count = 0;
        if (_atomic == 2) {
            acquire_read();
            for (unsigned i = 0; i < _stats.weight(); i++) {
                count += _stats.get_value(i).s._count;
                byte_count += _stats.get_value(i).s._byte_count;
            }
            release_read();
        } else {
            for (unsigned i = 0; i < _stats.weight(); i++) {
                _stats.get_value(i).lock.read_begin();
                count += _stats.get_value(i).s._count;
                byte_count += _stats.get_value(i).s._byte_count;
                _stats.get_value(i).lock.read_end();
            }
        }

        return {count,byte_count};
    }

    void add(stats s) override {
        _stats->s._count += s._count;
        _stats->s._byte_count += s._byte_count;
    }

    void atomic_add(stats s) override {
        _stats->lock.write_begin();
        _stats->s._count += s._count;
        _stats->s._byte_count += s._byte_count;
        _stats->lock.write_end();
    }

protected:
    class bucket { public:
        bucket() : s(), lock() {
        }
        stats s;
        RWLock lock;
    };
    per_thread<bucket> _stats CLICK_CACHE_ALIGN;

};

/**
 *
 * Counter duplicated per-thread with a global PRW lock
 *
 */
class CounterPRWMP : public CounterRWMP { public:

    CounterPRWMP() CLICK_COLD;
    ~CounterPRWMP() CLICK_COLD;

    const char *class_name() const      { return "CounterPRWMP"; }
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

/**
 * =c
 *
 * CounterRCU()
 *
 * Local EBSR RCU-based counter
 *
 * =s research
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

/**
 * =c
 *
 * CounterAtomic()
 *
 * Counter based on atomic operations
 *
 * =s research
 *
 */
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
