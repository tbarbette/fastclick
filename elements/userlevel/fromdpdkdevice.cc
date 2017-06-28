// -*- c-basic-offset: 4; related-file-name: "fromdpdkdevice.hh" -*-
/*
 * fromdpdkdevice.{cc,hh} -- element reads packets live from network via
 * Intel's DPDK
 *
 * Copyright (c) 2014-2015 Cyril Soldani, University of Liège
 * Copyright (c) 2016 Tom Barbette, University of Liège
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
#include <click/etheraddress.hh>

#include "fromdpdkdevice.hh"

CLICK_DECLS

FromDPDKDevice::FromDPDKDevice() :
    _dev(0), _active(true)
{
	#if HAVE_BATCH
		in_batch_mode = BATCH_MODE_YES;
	#endif
	_burst = 32;
	ndesc = 256;
}

FromDPDKDevice::~FromDPDKDevice()
{
}

int FromDPDKDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //Default parameters
    int numa_node = 0;
    String dev;
    EtherAddress mac;
    bool has_mac = false;

    if (parse(Args(conf, this, errh)
        .read_mp("PORT", dev))
        .read("NDESC", ndesc)
        .read("MAC", mac).read_status(has_mac)
        .read("ACTIVE", _active)
        .complete() < 0)
        return -1;

    if (!DPDKDeviceArg::parse(dev, _dev)) {
        if (allow_nonexistent)
            return 0;
        else
            return errh->error("%s : Unknown or invalid PORT", dev.c_str());
    }

    if (_use_numa) {
        numa_node = DPDKDevice::get_port_numa_node(_dev->port_id);
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

    if (has_mac)
        _dev->set_mac(mac);

    return 0;
}

int FromDPDKDevice::initialize(ErrorHandler *errh)
{
    int ret;

    if (!_dev)
        return 0;

    ret = initialize_rx(errh);
    if (ret != 0) return ret;

    for (int i = firstqueue; i < firstqueue + n_queues; i++) {
        ret = _dev->add_rx_queue(i , _promisc, ndesc, errh);
        if (ret != 0) return ret;
    }

    ret = initialize_tasks(_active,errh);
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

bool FromDPDKDevice::run_task(Task * t)
{
    struct rte_mbuf *pkts[_burst];
    int ret = 0;

    for (int iqueue = queue_for_thisthread_begin(); iqueue<=queue_for_thisthread_end();iqueue++) {
#if HAVE_BATCH
	 PacketBatch* head = 0;
     WritablePacket *last;
#endif
        unsigned n = rte_eth_rx_burst(_dev->port_id, iqueue, pkts, _burst);
        for (unsigned i = 0; i < n; ++i) {
            unsigned char* data = rte_pktmbuf_mtod(pkts[i], unsigned char *);
            rte_prefetch0(data);
#if CLICK_PACKET_USE_DPDK
            WritablePacket *p = Packet::make(pkts[i]);
#elif HAVE_ZEROCOPY
            WritablePacket *p = Packet::make(data,
                     rte_pktmbuf_data_len(pkts[i]),
                     DPDKDevice::free_pkt,
                     pkts[i],
                     rte_pktmbuf_headroom(pkts[i]),
                     rte_pktmbuf_tailroom(pkts[i])
                     );
#else
            WritablePacket *p = Packet::make(data,
                                     (uint32_t)rte_pktmbuf_pkt_len(pkts[i]));
            rte_pktmbuf_free(pkts[i]);
#endif
            p->set_packet_type_anno(Packet::HOST);
            p->set_mac_header(data);
            if (_set_rss_aggregate)
#if RTE_VERSION > RTE_VERSION_NUM(1,7,0,0)
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

String FromDPDKDevice::read_handler(Element *e, void * thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);

    switch((uintptr_t) thunk) {
        case h_active:
              if (!fd->_dev)
                  return "false";
              else
                  return "true";
        case h_mac: {
            if (!fd->_dev)
                return String::make_empty();
            struct ether_addr mac_addr;
            rte_eth_macaddr_get(fd->_dev->port_id, &mac_addr);
            return EtherAddress((unsigned char*)&mac_addr).unparse();
        }
    }

    return 0;
}

String FromDPDKDevice::status_handler(Element *e, void * thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    struct rte_eth_link link;
    if (!fd->_dev)
        return "0";

    rte_eth_link_get_nowait(fd->_dev->port_id, &link);
#ifndef ETH_LINK_UP
    #define ETH_LINK_UP 1
#endif
    switch((uintptr_t) thunk) {
      case h_carrier:
          return (link.link_status == ETH_LINK_UP ? "1" : "0");
      case h_duplex:
          return (link.link_status == ETH_LINK_UP ? (link.link_duplex == ETH_LINK_FULL_DUPLEX ? "1" : "0") : "-1");
#if RTE_VERSION >= RTE_VERSION_NUM(16,04,0,0)
      case h_autoneg:
          return String(link.link_autoneg);
#endif
      case h_speed:
          return String(link.link_speed);
    }
    return 0;
}

String FromDPDKDevice::statistics_handler(Element *e, void * thunk)
{
    FromDPDKDevice *fd = static_cast<FromDPDKDevice *>(e);
    struct rte_eth_stats stats;
    if (!fd->_dev)
        return "0";

    if (rte_eth_stats_get(fd->_dev->port_id, &stats))
        return String::make_empty();

    switch((uintptr_t) thunk) {
        case h_ipackets:
            return String(stats.ipackets);
        case h_ibytes:
            return String(stats.ibytes);
        case h_idropped:
            return String(stats.imissed);
        case h_ierrors:
            return String(stats.ierrors);
    }

    return 0;
}

void FromDPDKDevice::add_handlers()
{


    add_read_handler("duplex",status_handler, h_duplex);
#if RTE_VERSION >= RTE_VERSION_NUM(16,04,0,0)
    add_read_handler("autoneg",status_handler, h_autoneg);
#endif
    add_read_handler("speed",status_handler, h_speed);
    add_read_handler("carrier",status_handler, h_carrier);

    add_read_handler("active", read_handler, h_active);
    add_read_handler("count", count_handler, 0);
    add_read_handler("mac",read_handler, h_mac);

    add_read_handler("hw_count",statistics_handler, h_ipackets);
    add_read_handler("hw_bytes",statistics_handler, h_ibytes);
    add_read_handler("hw_dropped",statistics_handler, h_idropped);
    add_read_handler("hw_errors",statistics_handler, h_ierrors);

    add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);

    add_data_handlers("burst", Handler::h_read | Handler::h_write, &_burst);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel dpdk QueueDevice)
EXPORT_ELEMENT(FromDPDKDevice)
ELEMENT_MT_SAFE(FromDPDKDevice)
