/*
 * flowipmanager.{cc,hh} - Flow classification for the flow subsystem
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
#include <click/algorithm.hh>
#include <click/ipflowid.hh>
#include <click/routervisitor.hh>
#include <click/error.hh>
#include "flowipmanager.hh"
#include <rte_hash.h>
#include <click/dpdk_glue.hh>
#include <rte_ethdev.h>

CLICK_DECLS

FlowIPManager::FlowIPManager() : _verbose(1), _flags(0), _timer(this), _task(this), _cache(true), _qbsr(0), Router::InitFuture(this)
{
}

FlowIPManager::~FlowIPManager()
{
}

int
FlowIPManager::configure(Vector<String> &conf, ErrorHandler *errh)
{
    bool lf = false;
    int recycle_interval = 1;
    int timeout = 60;

    if (Args(conf, this, errh)
        .read_or_set_p("CAPACITY", _table_size, 65536)
        .read_or_set("RESERVE", _reserve, 0)
        .read_or_set("TIMEOUT", timeout, 60)
#if RTE_VERSION > RTE_VERSION_NUM(18,8,0,0)
        .read_or_set("LF", lf, false)
#endif
        .read_or_set("CACHE", _cache, true)
        .read_or_set("VERBOSE", _verbose, 1)
        .read_or_set("RECYCLE_INTERVAL", recycle_interval, 1)
        .complete() < 0)
        return -1;

    find_children(_verbose);

    router()->get_root_init_future()->postOnce(&_fcb_builded_init_future);
    _fcb_builded_init_future.post(this);

    if (!is_pow2(_table_size)) {
        _table_size = next_pow2(_table_size);
        click_chatter("Real capacity will be %d",_table_size);
    }

    _recycle_interval_ms = (int)(recycle_interval * 1000);
    _epochs_per_sec = max(1, (int)(1000 / _recycle_interval_ms));
    _timeout_ms = timeout * 1000;
    _timeout_epochs = timeout * _epochs_per_sec;


#if RTE_VERSION > RTE_VERSION_NUM(18,8,0,0)
    if (lf) {
        _flags &= ~RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY;
        _flags |= RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF | RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD;
    }
#endif

    _reserve += sizeof(uint32_t) + (_timeout_epochs > 0?sizeof(FlowControlBlock*):0);

    return 0;
}

int FlowIPManager::solve_initialize(ErrorHandler *errh)
{
    struct rte_hash_parameters hash_params = {0};
    char buf[32];
    hash_params.name = buf;
    hash_params.entries = _table_size;
    hash_params.key_len = sizeof(IPFlow5ID);
    hash_params.hash_func = ipv4_hash_crc;
    hash_params.hash_func_init_val = 0;
    hash_params.extra_flag = _flags;

    assert(_reserve >=  sizeof(uint32_t) + (_timeout_epochs > 0?sizeof(FlowControlBlock*):0));
    _flow_state_size_full = sizeof(FlowControlBlock) + _reserve;

    if (_verbose)
        errh->message("Per-flow size is %d", _reserve);
    sprintf(buf, "%s", name().c_str());
    hash = rte_hash_create(&hash_params);
    if (!hash)
        return errh->error("Could not init flow table !");

    fcbs =  (FlowControlBlock*)CLICK_ALIGNED_ALLOC(_flow_state_size_full * _table_size);
    bzero(fcbs,_flow_state_size_full * _table_size);
    CLICK_ASSERT_ALIGNED(fcbs);
    if (!fcbs)
        return errh->error("Could not init data table !");

    if (_timeout_epochs > 0) {
        if (_flags) {
            errh->warning("This element uses a timer wheel that will use a global lock. Consider using FlowIPManager_CuckooPP instead which uses per-thread timer wheels.");
        }


        _timer_wheel.initialize(_timeout_epochs);

        _timer.initialize(this);
        _timer.schedule_after(Timestamp::make_msec(_recycle_interval_ms));
        _task.initialize(this, false);

    }

    return Router::InitFuture::solve_initialize(errh);
}

/**
 * Returns the location of the IPFlow5ID in the FCB (first data)
 */
/*static inline IPFlow5ID *get_fcb_key(FlowControlBlock *fcb) {
    return (IPFlow5ID *)FCB_DATA(fcb, 0);
};*/
static inline uint32_t *get_fcb_flowid(FlowControlBlock *fcb) {
    return (uint32_t *)FCB_DATA(fcb, 0);
};

/**
 * Returns the location of the IPFlow5ID in the FCB (second data, after the key  when released)
 */
static inline FlowControlBlock** fcb_next_ptr(FlowControlBlock* fcb) {
     return (FlowControlBlock **)(fcb->data + sizeof(uint32_t));
}

/**
 * Returns the location of the IPFlow5ID in the FCB (secothird  data, after the next released ptr  when released)
 */
static inline uint32_t* get_released_fcb_flowid(FlowControlBlock* fcb) {
    return (uint32_t*)(get_fcb_flowid(fcb));
}


static const auto setter = [](FlowControlBlock* prev, FlowControlBlock* next)
{
    *fcb_next_ptr(prev) = next;
};

bool FlowIPManager::run_task(Task* t)
{
    Timestamp recent = Timestamp::recent_steady();
    while (_qbsr) {
        FlowControlBlock* next = *fcb_next_ptr(_qbsr);
        rte_hash_free_key_with_position(hash, *get_released_fcb_flowid(_qbsr));
        _qbsr = next;
    }
    _timer_wheel.run_timers([this,recent](FlowControlBlock* prev) -> FlowControlBlock*{
        FlowControlBlock* next = *fcb_next_ptr(prev);
        int old = (recent - prev->lastseen).msecval();
        if (old >= _timeout_ms) {
            if (unlikely(_verbose > 1))
                click_chatter("Release %p as it is expired since %d", prev, old);
            //expire
            void* keyptr;
            if (unlikely(rte_hash_get_key_with_position(hash, *get_fcb_flowid(prev),&keyptr) != 0)) {
                click_chatter("Could not get flow key");
            } else {
                int fid = rte_hash_del_key(hash, keyptr); //get_fcb_key(prev));
                //From now on the key is detroyed, the FCB can't be found
                //However in LF, a thread might be using the FCB still. It won't use the key
                //So the key space can be used to remember the next id
                if (this->_flags & RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF) {
                    *get_released_fcb_flowid(prev) = fid;
                    *fcb_next_ptr(prev) = _qbsr;
                    _qbsr = prev;
                }
            }
        } else {
            //No need for lock as we'll be the only one to enqueue there
            //int delta = (recent - prev->lastseen).sec();
            _timer_wheel.schedule_after(prev, _timeout_epochs, setter);
        }
        return next;
    });
    return true;
}

void FlowIPManager::run_timer(Timer* t)
{
    _task.reschedule();
    t->reschedule_after(Timestamp::make_msec(_recycle_interval_ms));
}

void FlowIPManager::cleanup(CleanupStage stage)
{
    if (hash)
        rte_hash_free(hash);
}

void FlowIPManager::process(Packet* p, BatchBuilder& b, const Timestamp& recent)
{
    IPFlow5ID fid = IPFlow5ID(p);

    if (_cache && fid == b.last_id) {
        b.append(p);
        return;
    }

    rte_hash*& table = hash;
    FlowControlBlock* fcb;
    int ret = rte_hash_lookup(table, &fid);

    if (ret < 0) { // new flow
        ret = rte_hash_add_key(table, &fid);
        if (unlikely(ret < 0)) {
            if (unlikely(_verbose > 0)) {
                click_chatter("Cannot add key (have %d items. Error %d)!", rte_hash_count(table), ret);
            }
            p->kill();
            return;
        }
        if (unlikely(_verbose > 1))
            click_chatter("New flow %d", ret);
        fcb = (FlowControlBlock*)((unsigned char*)fcbs + (_flow_state_size_full * ret));

        if (_timeout_epochs) {
            //memcpy(get_fcb_key(fcb), &fid, sizeof(IPFlow5ID));
            *get_fcb_flowid(fcb) = ret;

            if (_flags) {
                _timer_wheel.schedule_after_mp(fcb, _timeout_epochs, setter);
            } else {
                _timer_wheel.schedule_after(fcb, _timeout_epochs, setter);
            }
        }
    } else { //existing flow
        if (unlikely(_verbose > 1))
            click_chatter("Existing flow %d", ret);
        fcb = (FlowControlBlock*)((unsigned char*)fcbs + (_flow_state_size_full * ret));
    }

    if (b.last == ret) {
        b.append(p);
    } else {
        PacketBatch* batch;
        batch = b.finish();
        if (batch) {
            fcb_stack->lastseen = recent;
#if HAVE_FLOW_DYNAMIC
            fcb_acquire(batch->count());
#endif
            output_push_batch(0, batch);
        }
        fcb_stack = fcb;
        b.init();
        b.append(p);
        b.last = ret;
        if (_cache)
            b.last_id = fid;

    }
}

void FlowIPManager::push_batch(int, PacketBatch* batch)
{
    SFCB_STACK(
    BatchBuilder b;
    Timestamp recent = Timestamp::recent_steady();
    FOR_EACH_PACKET_SAFE(batch, p) {
        process(p, b, recent);
    }

    batch = b.finish();
    if (batch) {
        fcb_stack->lastseen = recent;
#if HAVE_FLOW_DYNAMIC
        fcb_acquire(batch->count());
#endif
        output_push_batch(0, batch);
    }
    );
}

enum {h_count};
String FlowIPManager::read_handler(Element* e, void* thunk)
{
    FlowIPManager* fc = static_cast<FlowIPManager*>(e);

    rte_hash* table = fc->hash;
    switch ((intptr_t)thunk) {
    case h_count:
        return String(rte_hash_count(table));
    default:
        return "<error>";
    }
};

void FlowIPManager::add_handlers()
{
    add_read_handler("count", read_handler, h_count);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(dpdk dpdk19 flow)
EXPORT_ELEMENT(FlowIPManager)
ELEMENT_MT_SAFE(FlowIPManager)
