/*
 * dpdkdevclock.{cc,hh} -- Clock using NIC hardware timestamp_
 * Tom Barbette
 *
 * Copyright (c) 2019 KTH Royal Institute of Technology
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
#include "dpdkdevclock.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/dpdkdevice.hh>
#include <rte_ethdev.h>
CLICK_DECLS

inline uint64_t get_current_tick_dpdk(void* thunk) {
#if RTE_VERSION < RTE_VERSION_NUM(19,8,0,0)
    return 0;
#else
    uint64_t t;
    if (rte_eth_read_clock(((DPDKDeviceClock*)thunk)->_dev->port_id, &t) != 0)
        return 0;
    return t;
#endif
}

inline uint64_t get_tick_hz_dpdk(void* thunk) {
    uint64_t begin = get_current_tick_dpdk(thunk);
    rte_delay_ms(100);
    uint64_t end = get_current_tick_dpdk(thunk);
    //Substract the time to get the time itself
    uint64_t freq = (end - begin) - (get_current_tick_dpdk(thunk) - get_current_tick_dpdk(thunk));
    return freq * 10;
}

DPDKDeviceClock::DPDKDeviceClock()
{
    _source.get_current_tick = &get_current_tick_dpdk;
    _source.get_tick_hz = &get_tick_hz_dpdk;
}

DPDKDeviceClock::~DPDKDeviceClock()
{

}

int
DPDKDeviceClock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_mp("PORT", _dev)
            .read("VERBOSE", _verbose)
            .read("INSTALL", _install)
            .complete() < 0)
        return -1;

#if RTE_VERSION < RTE_VERSION_NUM(19,8,0,0)
    return errh->error("DPDKDeviceClock needs DPDK 19.08");
#endif
    return 0;
}

int
DPDKDeviceClock::initialize(ErrorHandler *errh)
{
    return TSCClock::initialize(errh);
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(usertiming)
EXPORT_ELEMENT(DPDKDeviceClock)
ELEMENT_MT_SAFE(DPDKDeviceClock)
