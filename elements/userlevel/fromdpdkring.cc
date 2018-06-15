// -*- c-basic-offset: 4; related-file-name: "fromdpdkring.hh" -*-
/*
 * fromdpdkring.{cc,hh} -- element that reads packets from a circular
 * ring buffer using DPDK.
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
#include <click/standard/scheduleinfo.hh>

#include "fromdpdkring.hh"

CLICK_DECLS

FromDPDKRing::FromDPDKRing() :
    _task(this)
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif
}

FromDPDKRing::~FromDPDKRing()
{
}

int
FromDPDKRing::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (DPDKRing::parse(&args) != 0)
            return -1;

    if (args
            .complete() < 0)
        return -1;

    return 0;
}

int
FromDPDKRing::initialize(ErrorHandler *errh)
{
    if ( _burst_size == 0 ) {
        _burst_size = DPDKDevice::DEF_BURST_SIZE;
    }

    if ( (_ndesc > 0) && ((unsigned)_burst_size > _ndesc / 2) ) {
        errh->warning("[%s] BURST should not be greater than half the number of descriptors (%d)\n",
                        name().c_str(), _ndesc);
    }
    else if (_burst_size > DPDKDevice::DEF_BURST_SIZE) {
        errh->warning("[%s] BURST should not be greater than 32 as DPDK won't send more packets at once\n",
                        name().c_str());
    }

    if (DPDKDevice::initialize(errh) != 0)
        return -1;

    // If primary process, create the ring buffer and memory pool.
    // The primary process is responsible for managing the memory
    // and acting as a bridge to interconnect various secondary processes
    if (_force_create || (!_force_lookup && rte_eal_process_type() == RTE_PROC_PRIMARY)){
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
        return errh->error("[%s] Problem getting Rx ring. "
                    "Make sure that the involved processes have a correct ring configuration\n",
                    name().c_str());
    if ( !_message_pool )
        return errh->error("[%s] Problem getting message pool. "
                    "Make sure that the involved processes have a correct ring configuration\n",
                    name().c_str());

    // Schedule the element
    ScheduleInfo::initialize_task(this, &_task, true, errh);

    return 0;
}

void
FromDPDKRing::cleanup(CleanupStage)
{
}

bool
FromDPDKRing::run_task(Task *t)
{
    unsigned avail = 0;
#if HAVE_BATCH
    PacketBatch    *head = NULL;
    WritablePacket *last = NULL;
#endif

    struct rte_mbuf *pkts[_burst_size];

#if RTE_VERSION >= RTE_VERSION_NUM(17,5,0,0)
    int n = rte_ring_dequeue_burst(_ring, (void **)pkts, _burst_size, &avail);
#else
    int n = rte_ring_dequeue_burst(_ring, (void **)pkts, _burst_size);
    avail = n;
#endif
    if (n < 0) {
        click_chatter("[%s] Couldn't read from the Rx rings\n", name().c_str());
        return false;
    }

    // Turn the received frames into Click frames
    for (unsigned i = 0; i < n; ++i) {

    #if CLICK_PACKET_USE_DPDK
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
        WritablePacket *p = static_cast<WritablePacket*>(Packet::make(pkts[i]));
    #elif HAVE_ZEROCOPY
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
        WritablePacket *p = Packet::make(
            rte_pktmbuf_mtod(pkts[i], unsigned char *),
            rte_pktmbuf_data_len(pkts[i]),
            DPDKDevice::free_pkt,
            pkts[i],
            rte_pktmbuf_headroom(pkts[i]),
            rte_pktmbuf_tailroom(pkts[i])
        );

    #else
        WritablePacket *p = Packet::make(
            (void*)rte_pktmbuf_mtod(pkts[i], unsigned char *),
            (uint32_t)rte_pktmbuf_pkt_len(pkts[i]));
            rte_pktmbuf_free(pkts[i]
        );
    #endif

        p->set_packet_type_anno(Packet::HOST);

    #if HAVE_BATCH
        if (head == NULL)
            head = PacketBatch::start_head(p);
        else
            last->set_next(p);
        last = p;
    #else
        output(0).push(p);
    #endif

    }

#if HAVE_BATCH
    if (head) {
        head->make_tail  (last, n);
        output_push_batch(0, head);
    }
#endif
    _count += n;

    _task.fast_reschedule();

    return avail > 0;
}

String
FromDPDKRing::read_handler(Element *e, void *thunk)
{
    FromDPDKRing *fr = static_cast<FromDPDKRing*>(e);

    if ( thunk == (void *) 0 )
        return String(fr->_count);
}

void
FromDPDKRing::add_handlers()
{
    add_read_handler("count",  read_handler, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(FromDPDKRing)
ELEMENT_MT_SAFE(FromDPDKRing)
