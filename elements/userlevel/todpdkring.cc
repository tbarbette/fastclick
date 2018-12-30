/*
 * todpdkring.{cc,hh} -- element that sends packets to a circular ring buffer using DPDK.
 * Georgios Katsikas
 *
 * Copyright (c) 2016 KTH Royal Institute of Technology
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

#include "todpdkring.hh"

CLICK_DECLS

ToDPDKRing::ToDPDKRing() :
    _iqueue(),
    _internal_tx_queue_size(1024),
     _timeout(0),
    _blocking(false), _congestion_warning_printed(false),
    _dropped(0)
{

}

ToDPDKRing::~ToDPDKRing()
{
}

int
ToDPDKRing::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (DPDKRing::parse(&args) != 0)
            return -1;

    if (args
        .read("IQUEUE",       _internal_tx_queue_size)
        .read("BLOCKING",     _blocking)
        .read("TIMEOUT",      _timeout)
        .complete() < 0)
            return -1;

    return 0;
}

int
ToDPDKRing::initialize(ErrorHandler *errh)
{
#if HAVE_BATCH
    if (in_batch_mode == BATCH_MODE_YES) {
        if ( _burst_size > 0 )
            errh->warning("[%s] BURST is unused with batching!", name().c_str());
    } else
#endif
    {
        if ( _burst_size == 0 ) {
            _burst_size = DPDKDevice::DEF_BURST_SIZE;
            click_chatter("[%s] Non-positive BURST number. Setting default (%d) \n",
                    name().c_str(), _burst_size);
        }

        if ( (_ndesc > 0) && ((unsigned)_burst_size > _ndesc / 2) ) {
            errh->warning("[%s] BURST should not be greater than half the number of descriptors (%d)",
                    name().c_str(), _ndesc);
        }
    }

    if (DPDKDevice::initialize(errh) != 0)
        return -1;

    // If primary process, create the ring buffer and memory pool.
    // The primary process is responsible for managing the memory
    // and acting as a bridge to interconnect various secondary processes
    if (_force_create || (! _force_lookup && rte_eal_process_type() == RTE_PROC_PRIMARY)){

        _ring = rte_ring_create(
            _PROC_1.c_str(), DPDKDevice::RING_SIZE,
            rte_socket_id(), _flags
        );
    }
    // If secondary process, search for the appropriate memory and attach to it.
    else {
        _ring    = rte_ring_lookup   (_PROC_2.c_str());
    }

    _message_pool = rte_mempool_lookup(_MEM_POOL.c_str());

    if (!_message_pool) {
        _message_pool = rte_mempool_create(
            _MEM_POOL.c_str(), _ndesc,
            DPDKDevice::MBUF_DATA_SIZE,
            DPDKDevice::RING_POOL_CACHE_SIZE,
            DPDKDevice::RING_PRIV_DATA_SIZE,
            NULL, NULL, NULL, NULL,
            rte_socket_id(), _flags
        );
    }

    if ( !_ring )
        return errh->error("[%s] Problem getting Tx ring. "
                    "Make sure that the process attached to this element has the right ring configuration\n",
                    name().c_str());
    if ( !_message_pool )
        return errh->error("[%s] Problem getting message pool. "
                    "Make sure that the process attached to this element has the right ring configuration\n",
                    name().c_str());

    // Initialize the internal queue
    _iqueue.pkts = new struct rte_mbuf *[_internal_tx_queue_size];
    if (_timeout >= 0) {
        _iqueue.timeout.assign     (this);
        _iqueue.timeout.initialize (this);
        _iqueue.timeout.move_thread(click_current_cpu_id());
    }

    return 0;
}

void
ToDPDKRing::cleanup(CleanupStage stage)
{
    if ( _iqueue.pkts )
        delete[] _iqueue.pkts;
}

void
ToDPDKRing::run_timer(Timer *)
{
    flush_internal_tx_ring(_iqueue);
}

inline void
ToDPDKRing::set_flush_timer(DPDKDevice::TXInternalQueue &iqueue)
{
    if ( _timeout >= 0 ) {
        if ( iqueue.timeout.scheduled() ) {
            // No more pending packets, remove timer
            if ( iqueue.nr_pending == 0 )
                iqueue.timeout.unschedule();
        }
        else {
            if ( iqueue.nr_pending > 0 ) {
                // Pending packets, set timeout to flush packets
                // after a while even without burst
                if ( _timeout == 0 )
                    iqueue.timeout.schedule_now();
                else
                    iqueue.timeout.schedule_after_msec(_timeout);
            }
        }
    }
}

/* Flush as many packets as possible from the internal queue of the DPDK ring. */
void
ToDPDKRing::flush_internal_tx_ring(DPDKDevice::TXInternalQueue &iqueue)
{
    unsigned n;
    unsigned sent = 0;
    //
    // sub_burst is the number of packets DPDK should send in one call
    // if there is no congestion, normally 32. If it sends less, it means
    // there is no more room in the output ring and we'll need to come
    // back later. Also, if we're wrapping around the ring, sub_burst
    // will be used to split the burst in two, as rte_ring_enqueue_burst
    // needs a contiguous buffer space.
    //
    unsigned sub_burst;

    do {
        sub_burst = iqueue.nr_pending > DPDKDevice::DEF_BURST_SIZE ?
                DPDKDevice::DEF_BURST_SIZE : iqueue.nr_pending;

        // The sub_burst wraps around the ring
        if (iqueue.index + sub_burst >= _internal_tx_queue_size)
            sub_burst = _internal_tx_queue_size - iqueue.index;

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
        n = rte_ring_enqueue_burst(
            _ring, (void* const*)(&iqueue.pkts[iqueue.index]),
            sub_burst, 0
        );
#else
        n = rte_ring_enqueue_burst(
            _ring, (void* const*)(&iqueue.pkts[iqueue.index]),
            sub_burst
        );
#endif

        iqueue.nr_pending -= n;
        iqueue.index      += n;

        // Wrapping around the ring
        if (iqueue.index >= _internal_tx_queue_size)
            iqueue.index = 0;

        sent += n;
    } while ( (n == sub_burst) && (iqueue.nr_pending > 0) );

    _count += sent;

    // If ring is empty, reset the index to avoid wrap ups
    if (iqueue.nr_pending == 0)
        iqueue.index = 0;
}

void
ToDPDKRing::push(int, Packet *p)
{
    // Get the internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueue;

    bool congestioned;
    do {
        congestioned = false;

        // Internal queue is full
        if (iqueue.nr_pending == _internal_tx_queue_size) {
            // We just set the congestion flag. If we're in blocking mode,
            // we'll loop, else we'll drop this packet.
            congestioned = true;

            if ( !_blocking ) {
                ++_dropped;

                if ( !_congestion_warning_printed )
                    click_chatter("[%s] Packet dropped", name().c_str());
                _congestion_warning_printed = true;
            }
            else {
                if ( !_congestion_warning_printed )
                    click_chatter("[%s] Congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        }
        // There is space in the iqueue
        else {
            struct rte_mbuf *mbuf = DPDKDevice::get_mbuf(p, true, _numa_zone);
            if ( mbuf != NULL ) {
                iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _internal_tx_queue_size] = mbuf;
                iqueue.nr_pending++;
            }
        }

        // From FastClick mainstream: The following line seems to cause performance problems
        // so I slightly modified the policy to avoid stressing the queue so much.
        //if ( (int)iqueue.nr_pending >= _burst_size || congestioned ) {
        if ( ((int) iqueue.nr_pending > 0) || congestioned ) {
            flush_internal_tx_ring(iqueue);
        }
        // We wait until burst for sending packets, so flushing timer is especially important here
        set_flush_timer(iqueue);

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while ( unlikely(_blocking && congestioned) );

#if !CLICK_PACKET_USE_DPDK
    if ( likely(is_fullpush()) )
        p->kill_nonatomic();
    else
        p->kill();
#endif

}

#if HAVE_BATCH
void
ToDPDKRing::push_batch(int, PacketBatch *head)
{
    // Get the internal queue
    DPDKDevice::TXInternalQueue &iqueue = _iqueue;

    Packet *p    = head;
    Packet *next = NULL;

    // No recycling through Click if we have DPDK packets
    bool congestioned;
#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_START();
#endif

    do {
        congestioned = false;

        // First, place the packets in the queue, while there is still place there
        while ( iqueue.nr_pending < _internal_tx_queue_size && p ) {
            struct rte_mbuf *mbuf = DPDKDevice::get_mbuf(p, true, _numa_zone);
            if ( mbuf != NULL ) {
                iqueue.pkts[(iqueue.index + iqueue.nr_pending) % _internal_tx_queue_size] = mbuf;
                iqueue.nr_pending++;
            }
            next = p->next();

        #if !CLICK_PACKET_USE_DPDK
            BATCH_RECYCLE_PACKET_CONTEXT(p);
        #endif

            p = next;
        }

        // There are packets not pushed into the queue, congestion is very likely!
        if ( p != 0 ) {
            congestioned = true;
            if ( !_congestion_warning_printed ) {
                if ( !_blocking )
                    click_chatter("[%s] Packet dropped", name().c_str());
                else
                    click_chatter("[%s] Congestion warning", name().c_str());
                _congestion_warning_printed = true;
            }
        }

        // Flush the queue if we have pending packets
        if ( (int) iqueue.nr_pending > 0 ) {
            flush_internal_tx_ring(iqueue);
        }
        set_flush_timer(iqueue);

        // If we're in blocking mode, we loop until we can put p in the iqueue
    } while ( unlikely(_blocking && congestioned) );

#if !CLICK_PACKET_USE_DPDK
    // If non-blocking, drop all packets that could not be sent
    while (p) {
        next = p->next();
        BATCH_RECYCLE_PACKET_CONTEXT(p);
        p = next;
        ++_dropped;
    }
#endif

#if !CLICK_PACKET_USE_DPDK
    BATCH_RECYCLE_END();
#endif

}
#endif

String
ToDPDKRing::read_handler(Element *e, void *thunk)
{
    ToDPDKRing *tr = static_cast<ToDPDKRing*>(e);

    if ( thunk == (void *) 0 )
        return String(tr->_count);
    else
        return String(tr->_dropped);
}

void
ToDPDKRing::add_handlers()
{
    add_read_handler("n_sent",    read_handler, 0);
    add_read_handler("n_dropped", read_handler, 1);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(ToDPDKRing)
ELEMENT_MT_SAFE(ToDPDKRing)
