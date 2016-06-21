// -*- c-basic-offset: 4; related-file-name: "fromdpdkdevice.hh" -*-
/*
 * fromdpdkdevice.{cc,hh} -- element reads packets live from network via
 * Intel's DPDK
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
#include <click/standard/scheduleinfo.hh>

#include "fromdpdkdevice.hh"

CLICK_DECLS

FromDPDKDevice::FromDPDKDevice() :
    _port_id(0)
{
	#if HAVE_BATCH
		in_batch_mode = BATCH_MODE_YES;
	#endif
	_burst = 32;
}

FromDPDKDevice::~FromDPDKDevice()
{
}

int FromDPDKDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
	//Default parameters
    int numa_node = 0;

    if (parse(Args(conf, this, errh)
        .read_mp("PORT", _port_id))
        .read("NDESC", ndesc)
        .complete() < 0)
        return -1;

    if (_use_numa) {
        numa_node = DPDKDevice::get_port_numa_node(_port_id);
    }

    int r;
    if (n_queues == -1) {
	if (firstqueue == -1) {
		firstqueue = 0;
		//With DPDK we'll take as many queues as available threads
		 r = configure_rx(numa_node,1,128,errh);
	} else {
		//If a queue number is setted, user probably want only one queue
		r = configure_rx(numa_node,1,1,errh);
	}
    } else {
        if (firstqueue == -1)
            firstqueue = 0;
        r = configure_rx(numa_node,n_queues,n_queues,errh);
    }
    if (r != 0) return r;

    return 0;
}

int FromDPDKDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = initialize_rx(errh);
    if (ret != 0) return ret;

    for (int i = firstqueue; i < firstqueue + n_queues; i++) {
        ret = DPDKDevice::add_rx_device(_port_id, i , _promisc, ndesc, errh);
        if (ret != 0) return ret;
    }

    ret = initialize_tasks(true,errh);
    if (ret != 0) return ret;

    if (queue_share > 1)
        return errh->error("Sharing queue between multiple threads is not yet supported by FromDPDKDevice. Raise the number using N_QUEUES of queues or limit the number of threads using MAXTHREADS");

    if (all_initialized()) {
        ret = DPDKDevice::initialize(errh);
        if (ret != 0) return ret;
    }

    return ret;
}

void FromDPDKDevice::cleanup(CleanupStage)
{
	cleanup_tasks();
}

void FromDPDKDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

bool FromDPDKDevice::run_task(Task * t)
{
    struct rte_mbuf *pkts[_burst];
    int ret = 0;

    for (int iqueue = queue_for_thisthread_begin(); iqueue<=queue_for_thisthread_end();iqueue++) {
#if HAVE_BATCH
	 PacketBatch* head = 0;
     WritablePacket *last;
#endif
        unsigned n = rte_eth_rx_burst(_port_id, iqueue, pkts, _burst);
        for (unsigned i = 0; i < n; ++i) {
#if CLICK_PACKET_USE_DPDK
            rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
            WritablePacket *p = Packet::make(pkts[i]);
#elif HAVE_ZEROCOPY
    rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *));
    WritablePacket *p = Packet::make(rte_pktmbuf_mtod(pkts[i], unsigned char *),
                     rte_pktmbuf_data_len(pkts[i]),
					 DPDKDevice::free_pkt,
                     pkts[i],
                     rte_pktmbuf_headroom(pkts[i]),
                     rte_pktmbuf_tailroom(pkts[i])
                     );
#else
            WritablePacket *p = Packet::make((void*)rte_pktmbuf_mtod(pkts[i], unsigned char *),
                                     (uint32_t)rte_pktmbuf_pkt_len(pkts[i]));
            rte_pktmbuf_free(pkts[i]);
#endif
            p->set_packet_type_anno(Packet::HOST);
            if (_set_rss_aggregate)
#if RTE_VER_YEAR ||  RTE_VER_MAJOR > 1 || RTE_VER_MINOR > 7
                SET_AGGREGATE_ANNO(p,pkts[i]->hash.rss);
#else
                SET_AGGREGATE_ANNO(p,pkts[i]->pkt.hash.rss);
#endif
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
            head->make_tail(last,n);
            output_push_batch(0,head);
        }
#endif
        if (n) {
            add_count(n);
            ret = 1;
        }
    }

    /*We reschedule directly, as we cannot know if there is actually packet
     * available and dpdk has no select mechanism*/
    t->fast_reschedule();
    return (ret);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(FromDPDKDevice)
ELEMENT_MT_SAFE(FromDPDKDevice)
