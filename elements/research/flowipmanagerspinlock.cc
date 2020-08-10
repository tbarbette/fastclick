/*
 * FlowIPManagerSpinlock.{cc,hh} - SpinLock-protected version of FlowIPManger
 *
 * Copyright (c) 2019-2020 Tom Barbette, KTH Royal Institute of Technology
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
#include <click/glue.hh>
#include <click/args.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanagerspinlock.hh"
#include <rte_hash.h>
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS

Spinlock FlowIPManagerSpinlock::hash_table_lock;

FlowIPManagerSpinlock::FlowIPManagerSpinlock() : _verbose(1), _flags(0), _timer(this), _task(this)
{
}

FlowIPManagerSpinlock::~FlowIPManagerSpinlock()
{
}

int
FlowIPManagerSpinlock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_or_set_p("CAPACITY", _table_size, 65536)
        .read_or_set("RESERVE", _reserve, 0)
        .read_or_set("TIMEOUT", _timeout, 60)
        .complete() < 0)
        return -1;

    if (!is_pow2(_table_size)) {
        _table_size = next_pow2(_table_size);
        click_chatter("Real capacity will be %d",_table_size);
    }

    find_children(_verbose);

    router()->get_root_init_future()->postOnce(&_fcb_builded_init_future);
    _fcb_builded_init_future.post(this);

    return 0;
}


int FlowIPManagerSpinlock::solve_initialize(ErrorHandler *errh)
{
    struct rte_hash_parameters hash_params = {0};
    char buf[32];
    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    hash_params.extra_flag = _flags;
    FlowIPManagerSpinlock::hash_table_lock = Spinlock();

    _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

    sprintf(buf, "%s", name().c_str());
    hash = rte_hash_create(&hash_params);
    if (!hash)
        return errh->error("Could not init flow table !");

    fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _table_size);
    bzero(fcbs,_flow_state_size_full * _table_size);
    CLICK_ASSERT_ALIGNED(fcbs);
    if (!fcbs)
        return errh->error("Could not init data table !");

    if (_timeout > 0) {
        _timer_wheel.initialize(_timeout);
    }

    _timer.initialize(this);
    _timer.schedule_after(Timestamp::make_sec(1));
    _task.initialize(this, false);
    return 0;
}

const auto setter = [](FlowControlBlock* prev, FlowControlBlock* next)
{
    *((FlowControlBlock**)&prev->data_32[2]) = next;
};

bool FlowIPManagerSpinlock::run_task(Task* t)
{
    Timestamp recent = Timestamp::recent_steady();
    _timer_wheel.run_timers([this,recent](FlowControlBlock* prev) -> FlowControlBlock*{
        FlowControlBlock* next = *((FlowControlBlock**)&prev->data_32[2]);
        int old = (recent - prev->lastseen).sec();
        if (old > _timeout) {
            // click_chatter("Release %p as it is expired since %d", prev, old);
            // expire
            FlowIPManagerSpinlock::hash_table_lock.acquire();
            rte_hash_free_key_with_position(hash, prev->data_32[0]);
            FlowIPManagerSpinlock::hash_table_lock.release();
        } else {
            // click_chatter("Cascade %p", prev);
            // No need for lock as we'll be the only one to enqueue there
            _timer_wheel.schedule_after(prev, _timeout - (recent - prev->lastseen).sec(),setter);
        }
        return next;
    });
    return true;
}

void FlowIPManagerSpinlock::run_timer(Timer* t)
{
    _task.reschedule();
    t->reschedule_after(Timestamp::make_sec(1));
}

void FlowIPManagerSpinlock::cleanup(CleanupStage stage)
{
    if (hash)
    {
        FlowIPManagerSpinlock::hash_table_lock.acquire();
        rte_hash_free(hash);
        FlowIPManagerSpinlock::hash_table_lock.release();
    }
}

void FlowIPManagerSpinlock::process(Packet* p, BatchBuilder& b, const Timestamp& recent)
{
    IPFlow5ID fid = IPFlow5ID(p);
    rte_hash*& table = hash;
    FlowControlBlock* fcb;

    FlowIPManagerSpinlock::hash_table_lock.acquire();
    int ret = rte_hash_lookup(table, &fid);

    if (ret < 0) { // new flow
        ret = rte_hash_add_key(table, &fid);
        if (ret < 0) {
            if (unlikely(_verbose > 0)) {
                click_chatter("Cannot add key (have %d items. Error %d)!", rte_hash_count(table), ret);
            }
            p->kill();
            return;
        }
        fcb = (FlowControlBlock*)((unsigned char*)fcbs + (_flow_state_size_full * ret));
        fcb->data_32[0] = ret;
        if (_timeout) {
            if (_flags) {
                _timer_wheel.schedule_after_mp(fcb, _timeout, setter);
            } else {
                _timer_wheel.schedule_after(fcb, _timeout, setter);
            }
        }
    } else {
        fcb = (FlowControlBlock*)((unsigned char*)fcbs + (_flow_state_size_full * ret));
    }

    FlowIPManagerSpinlock::hash_table_lock.release();
    if (b.last == ret) {
        b.append(p);
    } else {
        PacketBatch* batch;
        batch = b.finish();
        if (batch) {
            fcb_stack->lastseen = recent;
            output_push_batch(0, batch);
        }
        fcb_stack = fcb;
        b.init();
        b.append(p);
    }
}

void FlowIPManagerSpinlock::push_batch(int, PacketBatch* batch)
{
    BatchBuilder b;
    Timestamp recent = Timestamp::recent_steady();
    FOR_EACH_PACKET_SAFE(batch, p) {
        process(p, b, recent);
    }

    batch = b.finish();
    if (batch) {
    fcb_stack->lastseen = recent;
        output_push_batch(0, batch);
    }
}

enum {h_count};
String FlowIPManagerSpinlock::read_handler(Element* e, void* thunk)
{
    FlowIPManagerSpinlock* fc = static_cast<FlowIPManagerSpinlock*>(e);

    rte_hash* table = fc->hash;
    switch ((intptr_t)thunk) {
    case h_count:
        FlowIPManagerSpinlock::hash_table_lock.acquire();
        return String(rte_hash_count(table));
        FlowIPManagerSpinlock::hash_table_lock.release();
    default:
        return "<error>";
    }
};

void FlowIPManagerSpinlock::add_handlers()
{
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(FlowIPManager)
EXPORT_ELEMENT(FlowIPManagerSpinlock)
ELEMENT_MT_SAFE(FlowIPManagerSpinlock)
