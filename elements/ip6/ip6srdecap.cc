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

IP6SRDecap::IP6SRDecap()
{

}

IP6SRDecap::~IP6SRDecap()
{
}

int
IP6SRDecap::configure(Vector<String> &conf, ErrorHandler *errh)
{
 
    if (Args(conf, this, errh)
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
    const IP6Address &a = DST_IP6_ANNO(p);

    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());

    click_ip6_sr *sr = (click_ip6_sr*)ip6_find_hdr(ip6, IP6_EH_ROUTING, p->end_data());
    if (sr == 0)
        return p;
    
    if (sr->segment_left == 0) {
        click_chatter("Invalid packet with 0 segments left?");
        return p;
    }
    sr->segment_left--;
    
    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRDecap)
ELEMENT_MT_SAFE(IP6SRDecap)
