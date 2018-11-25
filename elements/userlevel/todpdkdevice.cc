/*
 * todpdkdevice.{cc,hh} -- element sends packets to network via Intel's DPDK
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2015 Tom Barbette, University of Liège
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

#include <click/args.hh>
#include <click/error.hh>

#include "todpdkdevice.hh"

CLICK_DECLS

ToDPDKDevice::ToDPDKDevice() :
    _iqueues(), _dev(0),
    _timeout(0), _congestion_warning_printed(false)
{
     _blocking = false;
     _burst = -1;
     _internal_tx_queue_size = 1024;
     ndesc = 0;
}

ToDPDKDevice::~ToDPDKDevice()
{
}

int ToDPDKDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int maxqueues = 128;
    String dev;
    if (Args(this, errh).bind(conf)
            .read_mp("PORT", dev)
            .consume() < 0)
        return -1;


    if (parse(conf, errh) != 0)
        return -1;

    if (Args(conf, this, errh)
        .read("TIMEOUT", _timeout)
        .read("NDESC",ndesc)
        .read("MAXQUEUES", maxqueues)
        .complete() < 0)
            return -1;
    if (!DPDKDeviceArg::parse(dev, _dev)) {
        if (allow_nonexistent)
            return 0;
        else
            return errh->error("%s : Unknown or invalid PORT", dev.c_str());
    }

    //TODO : If user put multiple ToDPDKDevice with the same port and without the QUEUE parameter, try to share the available queues among them
    if (firstqueue == -1)
       firstqueue = 0;
    if (n_queues == -1) {
	configure_tx(1,maxqueues,errh);
    } else {
        configure_tx(n_queues,n_queues,errh);
    }

    return 0;
}

int ToDPDKDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_tx(errh);
    if (ret != 0)
        return ret;

    for (unsigned i = 0; i < (unsigned)n_queues; i++) {
        ret = _dev->add_tx_queue(i, ndesc , errh);
        if (ret != 0) return ret;    }

#if HAVE_BATCH
    if (batch_mode() == BATCH_MODE_YES) {
        if (_burst < 0)
            _burst = 1;
    } else
#endif
    {
        if (_burst < 0)
            _burst = 32;
    }

    if (ndesc > 0 && (unsigned)_burst > ndesc / 2 ) {
        errh->warning("BURST should not be upper than half the number of descriptor (%d)",ndesc);
    }

    if (_burst == 1 && _timeout == 0) {
        _timeout = -1;
    }

    ret = initialize_tasks(false,errh);
    if (ret != 0)
        return ret;

    for (unsigned i = 0; i < _iqueues.weight(); i++) {
        _iqueues.get_value(i).pkts = new struct rte_mbuf *[_internal_tx_queue_size];
        _iqueues.get_value(i).timeout.assign(this);
        _iqueues.get_value(i).timeout.initialize(this);
        _iqueues.get_value(i).timeout.move_thread(i);
    }

    _this_node = DPDKDevice::get_port_numa_node(_dev->port_id);

    //To set is_fullpush, we need to compute passing threads
    get_passing_threads();

    if (all_initialized()) {
        int ret = DPDKDevice::initialize(errh);
        if (ret != 0) return ret;
    }
    return 0;
}

void ToDPDKDevice::cleanup(CleanupStage)
{
    cleanup_tasks();
    for (unsigned i = 0; i < _iqueues.weight(); i++) {
        delete[] _iqueues.get_value(i).pkts;
    }
}

String ToDPDKDevice::statistics_handler(Element *e, void * thunk)
{
    ToDPDKDevice *td = static_cast<ToDPDKDevice *>(e);
    struct rte_eth_stats stats;
    if (!td->_dev)
        return "0";

    if (rte_eth_stats_get(td->_dev->port_id, &stats))
        return String::make_empty();

    switch((uintptr_t) thunk) {
        case h_opackets:
            return String(stats.opackets);
        case h_obytes:
            return String(stats.obytes);
        case h_oerrors:
            return String(stats.oerrors);
    }

    return 0;
}

void ToDPDKDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_read_handler("dropped", dropped_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);

    add_read_handler("hw_count",statistics_handler, h_opackets);
    add_read_handler("hw_bytes",statistics_handler, h_obytes);
    add_read_handler("hw_errors",statistics_handler, h_oerrors);
}

inline void ToDPDKDevice::set_flush_timer(DPDKDevice::TXInternalQueue &iqueue) {
    if (_timeout >= 0 || iqueue.nr_pending) {
        if (iqueue.timeout.scheduled()) {
            //No more pending packets, remove timer
            if (iqueue.nr_pending == 0)
                iqueue.timeout.unschedule();
        } else {
            if (iqueue.nr_pending > 0) {
                //Pending packets, set timeout to flush packets after a while even without burst
                if (_timeout <= 0)
                    iqueue.timeout.schedule_now();
                else
                    iqueue.timeout.schedule_after_msec(_timeout);
            }
        }
    }
}

void ToDPDKDevice::run_timer(Timer *)
{
    flush_internal_tx_queue(_iqueues.get());
}

/* Flush as much as possible packets from a given internal queue to the DPDK
 * device. */
void ToDPDKDevice::flush_internal_tx_queue(DPDKDevice::TXInternalQueue &iqueue) {
    unsigned sent = 0;
    unsigned r;
    /* sub_burst is the number of packets DPDK should send in one call if
     * there is no congestion, normally 32. If it sends less, it means
     * there is no more room in the output ring and we'll need to come
     * back later. Also, if we're wrapping around the ring, sub_burst
     * will be used to split the burst in two, as rte_eth_tx_burst needs a
     * contiguous buffer space.
     */
    unsigned sub_burst;

    lock(); // ! This is a queue lock, not a thread lock.

    do {
        sub_burst = iqueue.nr_pending > 32 ? 32 : iqueue.nr_pending;
        if (iqueue.index + sub_burst >= (unsigned)_internal_tx_queue_size)
            // The sub_burst wraps around the ring
            sub_burst = _internal_tx_queue_size - iqueue.index;
        //Todo : if there is multiple queue assigned to this thread, send on all of them
        r = rte_eth_tx_burst(_dev->port_id, queue_for_thisthread_begin(), &iqueue.pkts[iqueue.index],
                             sub_burst);
        iqueue.nr_pending -= r;
        iqueue.index += r;

        if (iqueue.index >= (unsigned)_internal_tx_queue_size) // Wrapping around the ring
            iqueue.index = 0;

        sent += r;
    } while (r == sub_burst && iqueue.nr_pending > 0);
    unlock();

    add_count(sent);
}

void ToDPDKDevice::push(int, Packet *p)
{
    // Get the thread-local internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueues.get();

    bool congestioned;
    do {
        congestioned = false;

        if (iqueue.nr_pending == (unsigned)_internal_tx_queue_size) { // Internal queue is full
            /* We just set the congestion flag. If we're in blocking mode,
             * we'll loop, else we'll drop this packet.*/
            congestioned = true;
            if (!_blocking) {
                add_dropped(1);
                if (!_congestion_warning_printed) {
                    click_chatter("%s: packet dropped", name().c_str());
                    _congestion_warning_printed = true;
                }
            } else if (!_congestion_warning_printed) {
                click_chatter("%s: congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        } else { // If there is space in the iqueue
            struct rte_mbuf* mbuf = DPDKDevice::get_mbuf(p, true, _this_node);
            if (mbuf != NULL) {
                iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _internal_tx_queue_size] = mbuf;
                iqueue.nr_pending++;
            }
        }

        if ((int)iqueue.nr_pending >= _burst || congestioned) {
            flush_internal_tx_queue(iqueue);
        }
        set_flush_timer(iqueue); //We wait until burst for sending packets, so flushing timer is especially important here

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

#if !CLICK_PACKET_USE_DPDK
    if (likely(is_fullpush()))
        p->kill_nonatomic();
    else
        p->kill();
#endif
}


/**
 * push_batch seems more complex than in tonetmapdevice, but it's only because
 *  we have to place pointers in an array, and we don't want to keep a linked
 *  list plus an array (we could end up with packets which were not sent in the
 *  array, and packets in the list, it would be a mess). So we use an array as
 *  a ring.
 */
#if HAVE_BATCH
void ToDPDKDevice::push_batch(int, PacketBatch *head)
{
    // Get the thread-local internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueues.get();

    Packet* p = head;
    Packet* next;

    //No recycling through click if we have DPDK-backed packets
    bool congestioned;
#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_START();
#endif
    do {
        congestioned = false;
        //First, place the packets in the queue
        while (iqueue.nr_pending < (unsigned)_internal_tx_queue_size && p) { // Internal queue is full
            // While there is still place in the iqueue
            struct rte_mbuf* mbuf = DPDKDevice::get_mbuf(p, true, _this_node);
            if (mbuf != NULL) {
                iqueue.pkts[(iqueue.index + iqueue.nr_pending) & (_internal_tx_queue_size - 1)] = mbuf;
                iqueue.nr_pending++;
            }
            next = p->next();
#if !CLICK_PACKET_USE_DPDK
            BATCH_RECYCLE_PACKET_CONTEXT(p);
#endif
            p = next;
        }

        if (p != 0) {
            congestioned = true;
            if (!_congestion_warning_printed) {
                if (!_blocking)
                    click_chatter("%s: packet dropped", name().c_str());
                else
                    click_chatter("%s: congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        }

        //Flush the queue if we have pending packets
        if ((int)iqueue.nr_pending >= _burst || congestioned) {
            flush_internal_tx_queue(iqueue);
        }
        set_flush_timer(iqueue);

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while (unlikely(_blocking && congestioned));

#if !CLICK_PACKET_USE_DPDK
    //If non-blocking, drop all packets that could not be sent
    while (p) {
        next = p->next();
        BATCH_RECYCLE_PACKET_CONTEXT(p);
        p = next;
        add_dropped(1);
    }
#endif

#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_END();
#endif

}
#endif

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(ToDPDKDevice)
ELEMENT_MT_SAFE(ToDPDKDevice)
