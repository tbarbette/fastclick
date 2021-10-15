/*
 * IP6SRProcess.{cc,hh} -- element encapsulates packet in IP6 header
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
#include "ip6srprocess.hh"
#include <click/nameinfo.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/glue.hh>
CLICK_DECLS

IP6SRProcess::IP6SRProcess()
{

}

IP6SRProcess::~IP6SRProcess()
{
}

int
IP6SRProcess::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}


Packet *
IP6SRProcess::simple_action(Packet *p_in)
{
    
    WritablePacket *p = p_in->push(0);
    if (!p)
        return 0;
   
    click_ip6 *ip6 = reinterpret_cast<click_ip6 *>(p->data());
    click_ip6_sr *sr = reinterpret_cast<click_ip6_sr *>(p->data() + sizeof(click_ip6));

    // Update IPv6 address according to the Segment Routing Header
    uint16_t address_offset = sizeof(click_ip6_sr) + sr->segment_left * sizeof(IP6Address);
    struct in6_addr addr;
    memcpy(&addr, sr + address_offset, sizeof(IP6Address));
    IP6Address new_addr = IP6Address(addr);
    SET_DST_IP6_ANNO(p, new_addr);

    // Update segment left of the SRH
    --sr->segment_left;

    // TODO: recompute the checksum with the new pseudo-header (the destination address has changed)

    // TODO: if segment left is 0, pass it to the upper layer or drop it (do it in the .click files)

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IP6SRProcess)
ELEMENT_MT_SAFE(IP6SRProcess)
