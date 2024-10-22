/*
 * RSSHashSwitch.{cc,hh} -- element demultiplexes packets based on hash of
 * specified packet fields
 * Eddie Kohler
 *
 * Computational batching support by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2018 Georgios Katsikas, RISE SICS
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
#include "rsshashswitch.hh"
#include <click/error.hh>
#include <click/args.hh>
#include <clicknet/ether.h>

#include <click/dpdkdevice.hh>
#include <click/ip6address.hh>
#include <click/ip6flowid.hh>
#include <rte_thash.h>

CLICK_DECLS

RSSHashSwitch::RSSHashSwitch()
{
}

#define RSS_HASH_KEY_LENGTH 40
static uint8_t hash_key[RSS_HASH_KEY_LENGTH] = {
0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
};

int
RSSHashSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _max = noutputs();
    if (Args(conf, this, errh)
        .read("MAX", _max)
        .complete() < 0)
    return -1;


    return 0;
}

int
RSSHashSwitch::process(Packet *p)
{
    union rte_thash_tuple tuple = {0};

    /* A complete tuple from the packet. */
    struct click_ether *eth = (click_ether*) p->mac_header();

    int len = 0;
    if (eth->ether_type == 0x0008) {

        IPFlowID f(p);
        tuple.v4.sport = f.sport();

        tuple.v4.dport = f.dport();
        IPAddress a = f.saddr();
        memcpy(&tuple.v4.src_addr, &a, 4);
        a = f.daddr();
        memcpy(&tuple.v4.dst_addr, &a, 4);
        len = 4 + 4 + 2 + 2;

    } else if  (eth->ether_type == 0xDD86) {
        IP6FlowID f(p);
        tuple.v6.sport = f.sport();
        tuple.v6.dport = f.dport();
        IP6Address a = f.saddr();
        memcpy(&tuple.v6.src_addr, &a, 16);
        a = f.daddr();
        memcpy(&tuple.v6.dst_addr, &a, 16);
        len = 16 + 16 + 2 + 2;
    } else {
        return 0;
    }

    uint32_t orig_hash = rte_softrss((uint32_t *)&tuple, len, hash_key);

    return orig_hash % _max;
}

void
RSSHashSwitch::push(int port, Packet *p)
{
    output(process(p)).push(p);
}

#if HAVE_BATCH
void
RSSHashSwitch::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this, port](Packet *p) { return process(p); };
    CLASSIFY_EACH_PACKET(_max + 1, fnt, batch, checked_output_push_batch);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(RSSHashSwitch)
ELEMENT_MT_SAFE(RSSHashSwitch)
ELEMENT_REQUIRES(dpdk)