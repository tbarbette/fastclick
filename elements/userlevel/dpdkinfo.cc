// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * dpdkinfo.{cc,hh} -- library for interfacing with dpdk
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
#include <click/straccum.hh>
#include "dpdkinfo.hh"

CLICK_DECLS

int DPDKInfo::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (instance) {
        return errh->error(
            "There can be only one instance of DPDKInfo!");
    }
    instance = this;
    bool has_socket_mbuf, has_mbuf = false;
    if (Args(conf, this, errh)
        .read_p("NB_MBUF", DPDKDevice::DEFAULT_NB_MBUF).read_status(has_mbuf)
        .read_all("NB_SOCKET_MBUF", DPDKDevice::NB_MBUF).read_status(has_socket_mbuf)
        .read("MBUF_SIZE", DPDKDevice::MBUF_DATA_SIZE)
        .read("MBUF_CACHE_SIZE", DPDKDevice::MBUF_CACHE_SIZE)
        .read("RX_PTHRESH", DPDKDevice::RX_PTHRESH)
        .read("RX_HTHRESH", DPDKDevice::RX_HTHRESH)
        .read("RX_WTHRESH", DPDKDevice::RX_WTHRESH)
        .read("TX_PTHRESH", DPDKDevice::TX_PTHRESH)
        .read("TX_HTHRESH", DPDKDevice::TX_HTHRESH)
        .read("TX_WTHRESH", DPDKDevice::TX_WTHRESH)
        .read("MEMPOOL_PREFIX", DPDKDevice::MEMPOOL_PREFIX)
        .read("DEF_RING_NDESC", DPDKDevice::DEF_RING_NDESC)
        .read("DEF_BURST_SIZE", DPDKDevice::DEF_BURST_SIZE)
        .read("RING_SIZE",      DPDKDevice::RING_SIZE)
        .read("RING_POOL_CACHE_SIZE", DPDKDevice::RING_POOL_CACHE_SIZE)
        .read("RING_PRIV_DATA_SIZE",  DPDKDevice::RING_PRIV_DATA_SIZE)
        .complete() < 0)
        return -1;

    if (has_mbuf) {
            if (!is_pow2(DPDKDevice::DEFAULT_NB_MBUF + 1)) {
                errh->warning("The number of MBUFs is (%d) not a power of 2 minus one. This will decrease performances.", DPDKDevice::DEFAULT_NB_MBUF );
            }
    }

    if (has_socket_mbuf) {
        for (int i = 0; i < DPDKDevice::NB_MBUF.size(); i++) {
            if (!is_pow2(DPDKDevice::NB_MBUF[i] + 1)) {
                errh->warning("The number of MBUFs for socket %d (%d) is not a power of 2 minus one. This will decrease performances.", i, DPDKDevice::NB_MBUF[i] );
            }
        }
    }

    if (DPDKDevice::MBUF_CACHE_SIZE > RTE_MEMPOOL_CACHE_MAX_SIZE) {
        return errh->error("The number of MBUF must be lower than %d", RTE_MEMPOOL_CACHE_MAX_SIZE);
    }

    return 0;
}

String DPDKInfo::read_handler(Element *e, void * thunk)
{
    StringAccum acc;
    switch((uintptr_t) thunk) {
        case h_pool_count:
            for (unsigned i = 0; i < DPDKDevice::_nr_pktmbuf_pools; i++) {
#if RTE_VERSION < RTE_VERSION_NUM(17,02,0,0)
                int avail = rte_mempool_count(DPDKDevice::_pktmbuf_pools[i]);
#else
                int avail = rte_mempool_avail_count(DPDKDevice::_pktmbuf_pools[i]);
#endif
                acc << String(i) << " " << String(avail) << "\n";
            }
            break;
        case h_pools:
            for (unsigned i = 0; i < DPDKDevice::_nr_pktmbuf_pools; i++) {
               acc << DPDKDevice::_pktmbuf_pools[i]->name << "\n";
            }
            break;
        default:
            acc << "<error>";
    }

    return acc.take_string();
}

void DPDKInfo::add_handlers() {
    add_read_handler("pool_count", read_handler, h_pool_count);
    add_read_handler("pools", read_handler, h_pools);
}

DPDKInfo* DPDKInfo::instance = 0;

CLICK_ENDDECLS

ELEMENT_REQUIRES(dpdk)
EXPORT_ELEMENT(DPDKInfo)
