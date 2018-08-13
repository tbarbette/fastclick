/*
 * extendedcounters.{cc,hh} -- research for locking on counters
 * Tom Barbette
 *
 * Copyright (c) 2016-2018 University of Liege
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "extendedcounters.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/args.hh>
#include <click/handlercall.hh>

CLICK_DECLS

CounterRxWMP::CounterRxWMP()
{
    _atomic = 2;
}

CounterRxWMP::~CounterRxWMP()
{
}

CounterRxWMPPR::CounterRxWMPPR()
{
    _atomic = 2;
}

CounterRxWMPPR::~CounterRxWMPPR()
{
}
CounterRxWMPPW::CounterRxWMPPW()
{
    _atomic = 2;
}

CounterRxWMPPW::~CounterRxWMPPW()
{
}



CounterLockMP::CounterLockMP()
{
    _atomic = 1;
}

CounterLockMP::~CounterLockMP()
{
}

CounterPLockMP::CounterPLockMP()
{
    _atomic = 2;
}

CounterPLockMP::~CounterPLockMP()
{
}



int
CounterLockMP::initialize(ErrorHandler *errh) {
    if (CounterBase::initialize(errh) != 0)
        return -1;
    //If not in simple mode, we only allow one writer so we can sum up the total number of threads

    return 0;
}

Packet*
CounterLockMP::simple_action(Packet *p)
{
    _stats->lock.acquire();
    _stats->s._count++;
    _stats->s._byte_count += p->length();
    _stats->lock.release();
    if (unlikely(!_simple))
        check_handlers(CounterLockMP::count(), CounterLockMP::byte_count()); //BUG : if not atomic, then handler may be called twice
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterLockMP::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch, p)
            CounterLockMP::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }

    _stats->lock.acquire();
    _stats->s._count += batch->count();
    _stats->s._byte_count += bc;
    _stats->lock.release();
    if (unlikely(!_simple))
        check_handlers(CounterLockMP::count(), CounterLockMP::byte_count());

    return batch;
}
#endif

void
CounterLockMP::reset()
{
    acquire();
    for (unsigned i = 0; i < _stats.weight(); i++) { \
        _stats.get_value(i).s._count = 0;
        _stats.get_value(i).s._byte_count = 0;
    }
    release();
    CounterBase::reset();
}

CounterRWMP::CounterRWMP()
{
    _atomic = 1;
}

CounterRWMP::~CounterRWMP()
{
}

CounterPRWMP::CounterPRWMP()
{
    _atomic = 2;
}

CounterPRWMP::~CounterPRWMP()
{
}



int
CounterRWMP::initialize(ErrorHandler *errh) {
    if (CounterBase::initialize(errh) != 0)
        return -1;
    //If not in simple mode, we only allow one writer so we can sum up the total number of threads

    return 0;
}

Packet*
CounterRWMP::simple_action(Packet *p)
{
    _stats->lock.write_begin();
    _stats->s._count++;
    _stats->s._byte_count += p->length();
    _stats->lock.write_end();
    if (unlikely(!_simple))
        check_handlers(CounterRWMP::count(), CounterRWMP::byte_count()); //BUG : if not atomic, then handler may be called twice
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterRWMP::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch, p)
            CounterRWMP::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }

    _stats->lock.write_begin();
    _stats->s._count += batch->count();
    _stats->s._byte_count += bc;
    _stats->lock.write_end();
    if (unlikely(!_simple))
        check_handlers(CounterRWMP::count(), CounterRWMP::byte_count());

    return batch;
}
#endif

void
CounterRWMP::reset()
{
    acquire_write();
    for (unsigned i = 0; i < _stats.weight(); i++) { \
        _stats.get_value(i).s._count = 0;
        _stats.get_value(i).s._byte_count = 0;
    }
    release_write();
    CounterBase::reset();
}


/*
CounterRCUMP::CounterRCUMP() : _stats()
{
}

CounterRCUMP::~CounterRCUMP()
{
}

Packet*
CounterRCUMP::simple_action(Packet *p)
{
    per_thread<stats>& stats = _stats.write_begin();
    stats->_count++;
    stats->_byte_count += p->length();
    if (unlikely(!_simple)) {
        PER_THREAD_MEMBER_SUM(counter_int_type,count,stats,_count);
        PER_THREAD_MEMBER_SUM(counter_int_type,byte_count,stats,_byte_count);
        check_handlers(count,byte_count);
    }
    _stats.write_commit();
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterRCUMP::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
                                CounterRCUMP::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    per_thread<stats>& stats = _stats.write_begin();
    stats->_count += batch->count();
    stats->_byte_count += bc;
    if (unlikely(!_simple)) {
        PER_THREAD_MEMBER_SUM(counter_int_type,count,stats,_count);
        PER_THREAD_MEMBER_SUM(counter_int_type,byte_count,stats,_byte_count);
        check_handlers(count,byte_count);
    }
    _stats.write_commit();
    return batch;
}
#endif

void
CounterRCUMP::reset()
{
    per_thread<stats>& stats = _stats.write_begin();
    for (unsigned i = 0; i < stats.weight(); i++) { \
        stats.get_value(i)._count = 0;
        stats.get_value(i)._byte_count = 0;
    }
    CounterBase::reset();
    _stats.write_commit();
}
*/
CounterRCU::CounterRCU()
{
}

CounterRCU::~CounterRCU()
{
}

Packet*
CounterRCU::simple_action(Packet *p)
{
    int flags;
    stats& stats = _stats.write_begin(flags);
    stats._count++;
    stats._byte_count += p->length();
    if (unlikely(!_simple)) {
        check_handlers(stats._count, stats._byte_count);
    }
    _stats.write_commit(flags);
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterRCU::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
                                CounterRCU::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    int flags;
    stats& stats = _stats.write_begin(flags);
    stats._count += batch->count();
    stats._byte_count += bc;
    if (unlikely(!_simple)) {
        check_handlers(stats._count, stats._byte_count);
    }
    _stats.write_commit(flags);
    return batch;
}
#endif

void
CounterRCU::reset()
{
    int flags;
    stats& stats = _stats.write_begin(flags);
    stats._count = 0;
    stats._byte_count = 0;
    CounterBase::reset();
    _stats.write_commit(flags);
}


CounterAtomic::CounterAtomic()
{
}

CounterAtomic::~CounterAtomic()
{
}

Packet*
CounterAtomic::simple_action(Packet *p)
{
    _count++;
    _byte_count += p->length();
    if (likely(_simple))
        return p;
    check_handlers(_count, _byte_count); //BUG : may run multiple times
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterAtomic::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
                        CounterAtomic::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    _count += batch->count();
    _byte_count += bc;
    if (likely(_simple))
        return batch;
    check_handlers(_count, _byte_count); //BUG : may run multiple times, use a lock when run is decided maybe?
    return batch;
}
#endif

void
CounterAtomic::reset()
{
    _count = 0;
    _byte_count = 0;
    CounterBase::reset();
}


CounterLock::CounterLock()
{
}

CounterLock::~CounterLock()
{
}

Packet*
CounterLock::simple_action(Packet *p)
{
    _lock.acquire();
    _count++;
    _byte_count += p->length();
    if (unlikely(!_simple)) {
        check_handlers(_count, _byte_count);
    }
    _lock.release();
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterLock::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
        CounterLock::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    _lock.acquire();
    _count += batch->count();
    _byte_count += bc;
    if (unlikely(!_simple)) {
        check_handlers(_count, _byte_count);
    }
    _lock.release();
    return batch;
}
#endif

void
CounterLock::reset()
{
    _lock.acquire();
    _count = 0;
    _byte_count = 0;
    CounterBase::reset();
    _lock.release();
}



CounterRW::CounterRW()
{
}

CounterRW::~CounterRW()
{
}

Packet*
CounterRW::simple_action(Packet *p)
{
    _lock.write_begin();
    _lock->_count++;
    _lock->_byte_count += p->length();
    if (unlikely(!_simple)) {
        check_handlers(_lock->_count, _lock->_byte_count);
    }
    _lock.write_end();
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterRW::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
        CounterRW::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    _lock.write_begin();
    _lock->_count += batch->count();
    _lock->_byte_count += bc;
    if (unlikely(!_simple)) {
        check_handlers(_lock->_count, _lock->_byte_count);
    }
    _lock.write_end();
    return batch;
}
#endif

void
CounterRW::reset()
{
    _lock.write_begin();
    _lock->_count = 0;
    _lock->_byte_count = 0;
    CounterBase::reset();
    _lock.write_end();
}

CounterPRW::CounterPRW()
{
}

CounterPRW::~CounterPRW()
{
}

Packet*
CounterPRW::simple_action(Packet *p)
{
    _lock.acquire_write();
    _s._count++;
    _s._byte_count += p->length();
    if (unlikely(!_simple)) {
        check_handlers(_s._count, _s._byte_count);
    }
    _lock.release_write();
    return p;
}

#if HAVE_BATCH
PacketBatch*
CounterPRW::simple_action_batch(PacketBatch *batch)
{
    if (unlikely(_batch_precise)) {
        FOR_EACH_PACKET(batch,p)
        CounterPRW::simple_action(p);
        return batch;
    }

    counter_int_type bc = 0;
    FOR_EACH_PACKET(batch,p) {
        bc += p->length();
    }
    _lock.acquire_write();
    _s._count += batch->count();
    _s._byte_count += bc;
    if (unlikely(!_simple)) {
        check_handlers(_s._count, _s._byte_count);
    }
    _lock.release_write();
    return batch;
}
#endif

void
CounterPRW::reset()
{
    _lock.acquire_write();
    _s._count = 0;
    _s._byte_count = 0;
    CounterBase::reset();
    _lock.release_write();
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(CounterRxWMP)
ELEMENT_MT_SAFE(CounterRxWMP)
EXPORT_ELEMENT(CounterRxWMPPR)
ELEMENT_MT_SAFE(CounterRxWMPPR)
EXPORT_ELEMENT(CounterRxWMPPW)
ELEMENT_MT_SAFE(CounterRxWMPPW)
EXPORT_ELEMENT(CounterLockMP)
ELEMENT_MT_SAFE(CounterLockMP)
EXPORT_ELEMENT(CounterPLockMP)
ELEMENT_MT_SAFE(CounterPLockMP)
EXPORT_ELEMENT(CounterRWMP)
ELEMENT_MT_SAFE(CounterRWMP)
EXPORT_ELEMENT(CounterPRWMP)
ELEMENT_MT_SAFE(CounterPRWMP)
EXPORT_ELEMENT(CounterRW)
ELEMENT_MT_SAFE(CounterRW)
EXPORT_ELEMENT(CounterPRW)
ELEMENT_MT_SAFE(CounterPRW)
EXPORT_ELEMENT(CounterAtomic)
ELEMENT_MT_SAFE(CounterAtomic)
EXPORT_ELEMENT(CounterRCU)
ELEMENT_MT_SAFE(CounterRCU)
/*EXPORT_ELEMENT(CounterRCUMP)
ELEMENT_MT_SAFE(CounterRCUMP)*/
EXPORT_ELEMENT(CounterLock)
ELEMENT_MT_SAFE(CounterLock)
