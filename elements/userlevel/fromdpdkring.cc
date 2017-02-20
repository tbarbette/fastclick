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
    _task(this), _message_pool(0), _recv_ring(0),
    _numa_zone(0), _burst_size(0),
    _pkts_recv(0), _bytes_recv(0)
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif

    _ndesc = DPDKDevice::DEF_RING_NDESC;
    _def_burst_size = DPDKDevice::DEF_BURST_SIZE;
}

FromDPDKRing::~FromDPDKRing()
{
}

int
FromDPDKRing::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read_mp("MEM_POOL",  _MEM_POOL)
        .read_mp("FROM_PROC", _origin)
        .read_mp("TO_PROC",   _destination)
        .read("BURST",        _burst_size)
        .read("NDESC",        _ndesc)
        .read("NUMA_ZONE",    _numa_zone)
        .complete() < 0)
        return -1;

    if ( _MEM_POOL.empty() || (_MEM_POOL.length() == 0) ) {
        return errh->error("[%s] Enter FROM_PROC and TO_PROC names", name().c_str());
    }

    if ( _origin.empty() || _destination.empty() ) {
        return errh->error("[%s] Enter FROM_PROC and TO_PROC names", name().c_str());
    }

    _MEM_POOL = DPDKDevice::MEMPOOL_PREFIX + _MEM_POOL;

    if ( _ndesc == 0 ) {
        _ndesc = DPDKDevice::DEF_RING_NDESC;
        click_chatter("[%s] Default number of descriptors is set (%d)\n",
                        name().c_str(), _ndesc);
    }

    // If user does not specify the port number
    // we assume that the process belongs to the
    // memory zone of device 0.
    if ( _numa_zone < 0 ) {
        click_chatter("[%s] Assuming NUMA zone 0\n", name().c_str());
        _numa_zone = 0;
    }

    _PROC_1 = _origin+"_2_"+_destination;
    _PROC_2 = _destination+"_2_"+_origin;

    return 0;
}

int
FromDPDKRing::initialize(ErrorHandler *errh)
{
    if ( _burst_size == 0 ) {
        _burst_size = _def_burst_size;
        errh->warning("[%s] Non-positive BURST number. Setting default (%d)\n",
                        name().c_str(), _burst_size);
    }

    if ( (_ndesc > 0) && ((unsigned)_burst_size > _ndesc / 2) ) {
        errh->warning("[%s] BURST should not be greater than half the number of descriptors (%d)\n",
                        name().c_str(), _ndesc);
    }
    else if (_burst_size > _def_burst_size) {
        errh->warning("[%s] BURST should not be greater than 32 as DPDK won't send more packets at once\n",
                        name().c_str());
    }

    // If primary process, create the ring buffer and memory pool.
    // The primary process is responsible for managing the memory
    // and acting as a bridge to interconnect various secondary processes
    if ( rte_eal_process_type() == RTE_PROC_PRIMARY ){
        _recv_ring = rte_ring_create(
            _PROC_1.c_str(), DPDKDevice::RING_SIZE,
            rte_socket_id(), DPDKDevice::RING_FLAGS
        );
    }
    // If secondary process, search for the appropriate memory and attach to it.
    else {
        _recv_ring    = rte_ring_lookup   (_PROC_2.c_str());
    }

    _message_pool = rte_mempool_lookup(_MEM_POOL.c_str());
    if (!_message_pool) {
        _message_pool = rte_mempool_create(
            _MEM_POOL.c_str(), _ndesc,
            DPDKDevice::MBUF_DATA_SIZE,
            DPDKDevice::RING_POOL_CACHE_SIZE,
            DPDKDevice::RING_PRIV_DATA_SIZE,
            NULL, NULL, NULL, NULL,
            rte_socket_id(), DPDKDevice::RING_FLAGS
        );
    }


    if ( !_recv_ring )
        return errh->error("[%s] Problem getting Rx ring. "
                    "Make sure that the involved processes have a correct ring configuration\n",
                    name().c_str());
    if ( !_message_pool )
        return errh->error("[%s] Problem getting message pool. "
                    "Make sure that the involved processes have a correct ring configuration\n",
                    name().c_str());

    // The other end of this element might be in a different process (hence Click configuration),
    // thus it is important to make sure that the configuration of that element agrees with ours.
    /*
    click_chatter("[%s] Initialized with the following options: \n", name().c_str());
    click_chatter("|->  MEM_POOL: %s \n", _MEM_POOL.c_str());
    click_chatter("|-> FROM_PROC: %s \n", _origin.c_str());
    click_chatter("|->   TO_PROC: %s \n", _destination.c_str());
    click_chatter("|-> NUMA ZONE: %d \n", _numa_zone);
    click_chatter("|->     BURST: %d \n", _burst_size);
    click_chatter("|->     NDESC: %d \n", _ndesc);
    */

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
#if HAVE_BATCH
    PacketBatch    *head = NULL;
    WritablePacket *last = NULL;
#endif

    struct rte_mbuf *pkts[_burst_size];

    int n = rte_ring_dequeue_burst(_recv_ring, (void **)pkts, _burst_size);
    if ( n < 0 ) {
        click_chatter("[%s] Couldn't read from the Rx rings\n", name().c_str());
        return false;
    }

    // Turn the received frames into Click frames
    for (unsigned i = 0; i < n; ++i) {

    #if CLICK_PACKET_USE_DPDK
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
        WritablePacket *p = Packet::make(pkts[i]);

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

        // Keep statistics
        _bytes_recv += p->length();
    }

#if HAVE_BATCH
    if (head) {
        head->make_tail  (last, n);
        output_push_batch(0, head);
    }
#endif
    _pkts_recv += n;

    _task.fast_reschedule();

    return true;
}

String
FromDPDKRing::read_handler(Element *e, void *thunk)
{
    FromDPDKRing *fr = static_cast<FromDPDKRing*>(e);

    if ( thunk == (void *) 0 )
        return String(fr->_pkts_recv);
    else
        return String(fr->_bytes_recv);
}

void
FromDPDKRing::add_handlers()
{
    add_read_handler("pkt_count",  read_handler, 0);
    add_read_handler("byte_count", read_handler, 1);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(FromDPDKRing)
ELEMENT_MT_SAFE(FromDPDKRing)
