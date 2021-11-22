// -*- related-file-name: "../include/click/ipflowid.hh" -*-
/*
 * ipflowid.{cc,hh} -- a TCP-UDP/IP flow identifier class.
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008-2009 Meraki, Inc.
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
#include <click/ipflowid.hh>
#include <clicknet/ip.h>
#include <clicknet/udp.h>
#include <click/packet.hh>
#include <click/confparse.hh>
#include <click/straccum.hh>
CLICK_DECLS

IPFlowID::IPFlowID(const Packet *p, bool reverse)
{
    const click_ip *iph = p->ip_header();
    const click_udp *udph = p->udp_header();

    if (!p->has_network_header()   ||
        !p->has_transport_header() ||
        !IP_FIRSTFRAG(iph)) {
        return;
    }

    if (likely(!reverse))
	assign(iph->ip_src.s_addr, udph->uh_sport,
	       iph->ip_dst.s_addr, udph->uh_dport);
    else
	assign(iph->ip_dst.s_addr, udph->uh_dport,
	       iph->ip_src.s_addr, udph->uh_sport);
}

IPFlowID::IPFlowID(const click_ip *iph, bool reverse)
{
    if (!iph || !IP_FIRSTFRAG(iph)) {
        return;
    }

    const click_udp *udph = reinterpret_cast<const click_udp *>(reinterpret_cast<const unsigned char *>(iph) + (iph->ip_hl << 2));

    if (likely(!reverse))
	assign(iph->ip_src.s_addr, udph->uh_sport,
	       iph->ip_dst.s_addr, udph->uh_dport);
    else
	assign(iph->ip_dst.s_addr, udph->uh_dport,
	       iph->ip_src.s_addr, udph->uh_sport);
}

int
IPFlowID::unparse(char *s) const
{
    if (s) {
	const unsigned char *p = _saddr.data();
	const unsigned char *q = _daddr.data();
	return sprintf(s, "(%d.%d.%d.%d, %hu, %d.%d.%d.%d, %hu)",
		       p[0], p[1], p[2], p[3], ntohs(_sport),
		       q[0], q[1], q[2], q[3], ntohs(_dport));
    } else
	return 0;
}

String
IPFlowID::unparse() const
{
    char tmp[64];
    return String(tmp, unparse(tmp));
}

StringAccum &
operator<<(StringAccum &sa, const IPFlowID &flow_id)
{
    sa.adjust_length(flow_id.unparse(sa.reserve(64)));
    return sa;
}

IPFlow5ID::IPFlow5ID(const Packet *p, bool reverse) : IPFlowID(p,reverse) {
	_proto = p->ip_header()->ip_p;
    _zero = 0;
}


int
IPFlow5ID::unparse(char *s) const
{
    if (s) {
	const unsigned char *p = _saddr.data();
	const unsigned char *q = _daddr.data();
	return sprintf(s, "(%d: %d.%d.%d.%d, %hu, %d.%d.%d.%d, %hu)", _proto,
		       p[0], p[1], p[2], p[3], ntohs(_sport),
		       q[0], q[1], q[2], q[3], ntohs(_dport));
    } else
	return 0;
}

String
IPFlow5ID::unparse() const
{
    char tmp[64];
    return String(tmp, unparse(tmp));
}


CLICK_ENDDECLS
