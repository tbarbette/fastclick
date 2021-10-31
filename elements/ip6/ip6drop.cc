/*
 * IP6SREncap.{cc,hh} -- element encapsulates packet in IP6 header
 * Roman Chertov
 *
 * Copyright (c) 2008 Santa Barbara Labs, LLC
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
#include "ip6drop.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IP6Drop::IP6Drop()
{
    _use_dst_anno = false;
}

IP6Drop::~IP6Drop()
{
}

int
IP6Drop::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_all("ADDR", addrs)
    .read_or_set("P", p, 0)
    .read_or_set("R", r, 0)
    .read_or_set("H", h, 0)
    .read_or_set("K", k, 1)
    .read_or_set("SEED", seed, 51)
	.complete() < 0)
        return -1;

    total_seen = 0;
    srand(seed);
    state = good;

    return 0;
}


Packet *
IP6Drop::simple_action(Packet *p_in)
{
    // // Do not drop the repair symbols
    // // TODO: adapt if we change and do not use ping anymore
    // int idxs[] = {2, 3, 7};
    // if (p_in->length() > 200) return p_in;
    // total_seen++;
    // for (int i = 0; i < 1; ++i) {
    //     if (total_seen % 20 == idxs[i]) {
    //         click_chatter("Drop packet");
    //         return 0;
    //         }
    // }
    // return p_in;
    const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p_in->data());
    uint32_t *dst_32 = (uint32_t *)&ip6->ip6_dst;
    total_seen++;
    bool found = false;
    for (int i = 0; i < addrs.size(); ++i) {
        IP6Address addr = addrs.at(i);
        uint32_t *addr_32 = (uint32_t *)addr.data32();
        if (addr_eq(addr_32, dst_32)) {
            found = true;
            break;
        }
    }
    if (!found) return p_in;

    if (!gemodel()) {
        click_chatter("Drop packet #%u", total_seen);
        return 0;
    }
    return p_in;
}

bool
IP6Drop::gemodel()
{
    bool keep_packet = true;
    bool change_state = false;
    if (state == good) {
        keep_packet = (rand() % 100) <= k * 100;
        change_state = (rand() % 100) <= p * 100;
        if (change_state) {
            state = bad;
        }
    } else {
        keep_packet = (rand() % 100) <= h * 100;
        change_state = (rand() % 100) <= r * 100;
        if (change_state) {
            state = good;
        }
    }
    return keep_packet;
}

bool
IP6Drop::addr_eq(uint32_t *a1, uint32_t *a2)
{
    return (a1[0] == a2[0] && a1[1] == a2[1] && a1[2] == a2[2] && a1[3] == a2[3]);
}

String
IP6Drop::read_handler(Element *e, void *thunk)
{
    return "<error>";
}

void
IP6Drop::add_handlers()
{
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6Drop)
ELEMENT_MT_SAFE(IP6Drop)
