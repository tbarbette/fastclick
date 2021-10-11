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
#include "ip6srencap.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IP6SREncap::IP6SREncap()
{

}

IP6SREncap::~IP6SREncap()
{
}

int
IP6SREncap::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Vector<IP6Address> addr;

    if (Args(conf, this, errh)
	.read_all("ADDR", addr)
	.complete() < 0)
        return -1;
    assert(sizeof(click_ip6_sr) == 8);
    _sr_len = sizeof(click_ip6_sr) + addr.size() * sizeof(IP6Address);
    _sr = (click_ip6_sr*)CLICK_LALLOC(_sr_len);
    _sr->ip6_hdrlen = sizeof(IP6Address) * addr.size() / 8;
    _sr->type = 4;
	_sr->segment_left = addr.size() - 1;
	_sr->last_entry = addr.size() - 1;
	_sr->flags = 0;
    _sr->tag = 0;
    memcpy(_sr->segments, addr.data(), sizeof(IP6Address) * addr.size());

    return 0;
}


Packet *
IP6SREncap::simple_action(Packet *p_in)
{
    
    WritablePacket *p = p_in->push(_sr_len);
    if (!p)
        return 0;
   
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *sr = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));
    click_chatter("V1: %u %u %u", _sr->ip6_hdrlen, _sr->segment_left, _sr->last_entry);
    memcpy(ip6, p->data() + _sr_len, sizeof(click_ip6));
    memcpy(sr, _sr, _sr_len);
    click_chatter("V2: %u %u %u %u", _sr->ip6_hdrlen, _sr->segment_left, _sr->last_entry, _sr_len);
    click_chatter("V3: %u %u %u", sr->ip6_hdrlen, sr->segment_left, sr->last_entry);
    sr->ip6_sr_next =  ip6->ip6_nxt;
    ip6->ip6_nxt = IP6_EH_ROUTING;

    // TODO: update IPv6 header payload length

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SREncap)
ELEMENT_MT_SAFE(IP6SREncap)
