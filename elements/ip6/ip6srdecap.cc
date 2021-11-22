/*
 * IP6SRDecap.{cc,hh} -- element encapsulates packet in IP6 header
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
#include "ip6srdecap.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IP6SRDecap::IP6SRDecap() : _force(false)
{

}

IP6SRDecap::~IP6SRDecap()
{
}

int
IP6SRDecap::configure(Vector<String> &conf, ErrorHandler *errh)
{
 
    if (Args(conf, this, errh)
        .read("FORCE_DECAP", _force)
	    .complete() < 0)
        return -1;

    
    return 0;
}


Packet *
IP6SRDecap::simple_action(Packet *p_in)
{
    
    WritablePacket *p = p_in->uniqueify();
    if (!p)
        return 0;

    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *sr = (click_ip6_sr*)ip6_find_header(ip6, IP6_EH_ROUTING, p->end_data());

    if (sr == 0)
        return p;
    
    if (_force || sr->segment_left == 1) {

            IP6Address last = IP6Address(sr->segments[0]);

            unsigned char* old_data = p->data();
            unsigned char nxt = sr->ip6_sr_next;
            unsigned char *next_ptr = (unsigned char*)ip6_find_header(ip6, nxt, p->end_data());
            unsigned offset = p->transport_header_offset();
            if (next_ptr == 0) {
                p->kill();
                click_chatter("Cannot find next header %d. Buggy packet?", nxt);
                return 0;
            }
            unsigned srlen = (unsigned char*)next_ptr - (unsigned char*)sr;
            p->pull(srlen);

            memmove(p->data(), old_data, (unsigned char*)sr-old_data);
            ip6 = (click_ip6 *)(p->data());

            ip6->ip6_dst = last;
            ip6->ip6_nxt = nxt;
            ip6->ip6_plen = htons(ntohs(ip6->ip6_plen));
            p->set_network_header(p->data(), offset);


    } else if (unlikely(sr->segment_left == 0)) {
        click_chatter("Invalid packet with 0 segments left?");
        return p;
    } else {
        ip6->ip6_dst = sr->segments[--sr->segment_left];
    }

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRDecap)
ELEMENT_MT_SAFE(IP6SRDecap)
