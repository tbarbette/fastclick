/*
 * fromdpdkdevice.{cc,hh} -- element reads packets live from network via
 * Intel's DPDK
 *
 * Copyright (c) 2014-2015 University of Liège
 * Copyright (c) 2014 Cyril Soldani
 * Copyright (c) 2015 Tom Barbette
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

#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include "fromdpdkdevice.hh"

CLICK_DECLS

FromDpdkDevice::FromDpdkDevice()
    : _port_no(0), _promisc(true), _burst(32)
{
}

FromDpdkDevice::~FromDpdkDevice()
{
}

int FromDpdkDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
	//Default parameters
    int maxthreads = -1;
    int threadoffset = -1;
    int minqueues = 1;
    int maxqueues = 128; //TODO Should be device dependent

    if (Args(conf, this, errh)
	.read_mp("DEVNAME", _port_no)
	.read_p("PROMISC", _promisc)
	.read_p("BURST", _burst)
	.read_p("MAXTHREADS", maxthreads)
	.read_p("THREADOFFSET", threadoffset)
	.read("MINQUEUES",minqueues)
	.read("MAXQUEUES",maxqueues)
	.read("NDESC",ndesc)
	.complete() < 0)
	return -1;

    int numa_node = DpdkDevice::get_port_numa_node(_port_no);


    int r;
    r = QueueDevice::configure_rx(numa_node,maxthreads,minqueues,maxqueues,threadoffset,errh);
    if (r != 0) return r;

    return 0;
}

int FromDpdkDevice::initialize(ErrorHandler *errh)
{
    int ret;

    ret = QueueDevice::initialize_rx(errh);
    if (ret != 0) return ret;

    for (int i = 0; i < nqueues; i++) {
        ret = DpdkDevice::add_rx_device(_port_no, i , _promisc, errh);
        if (ret != 0) return ret;
    }

    if (ndesc > 0)
        DpdkDevice::set_rx_descs(_port_no, ndesc, errh);

    ret = QueueDevice::initialize_tasks(true,errh);
    if (ret != 0) return ret;


    if (all_initialized()) {
        ret = DpdkDevice::initialize(errh);
        if (ret != 0) return ret;
    }

    return ret;
}

void FromDpdkDevice::add_handlers()
{
    add_read_handler("count", count_handler, 0);
    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

bool FromDpdkDevice::run_task(Task * t)
{
    struct rte_mbuf *pkts[_burst];
    int ret = 0;

    for (int iqueue = queue_for_thread_begin(); iqueue<=queue_for_thread_end();iqueue++) {
        unsigned n = rte_eth_rx_burst(_port_no, iqueue, pkts, _burst);
        for (unsigned i = 0; i < n; ++i) {
            WritablePacket *p = Packet::make((void*)rte_pktmbuf_mtod(pkts[i], unsigned char *),
                                     (uint32_t)rte_pktmbuf_pkt_len(pkts[i]));
            rte_pktmbuf_free(pkts[i]);
            p->set_packet_type_anno(HOST);
            output(0).push(p);
        }
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
EXPORT_ELEMENT(FromDpdkDevice)
ELEMENT_MT_SAFE(FromDpdkDevice)
