/*
 * automarkipheader.{cc,hh} -- element sets IP Header annotation
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 2017 KTH Royal Institute of Technology
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
#include "automarkipheader.hh"
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/ether.h>
CLICK_DECLS

AutoMarkIPHeader::AutoMarkIPHeader() :
_ethertype_8021q(htons(ETHERTYPE_8021Q)),
_ethertype_ip(htons(ETHERTYPE_IP)),
_ethertype_ip6(htons(ETHERTYPE_IP6))
{
}

AutoMarkIPHeader::~AutoMarkIPHeader()
{
}

Packet *
AutoMarkIPHeader::simple_action(Packet *p)
{
	assert(!p->mac_header() || p->mac_header() == p->data());
	const click_ether_vlan *vlan = reinterpret_cast<const click_ether_vlan *>(p->data());

	if (vlan->ether_vlan_proto == _ethertype_8021q) {
		if (vlan->ether_vlan_encap_proto == _ethertype_ip) {
			const click_ip *ip = reinterpret_cast<const click_ip *>(p->data() + sizeof(click_ether_vlan));
			p->set_ip_header(ip, ip->ip_hl << 2);
		} else if (vlan->ether_vlan_encap_proto == _ethertype_ip6) {
			const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p->data() + sizeof(click_ether_vlan));
			p->set_ip6_header(ip6, 10 << 2);
		}
	}
	else if (vlan->ether_vlan_proto == _ethertype_ip) {
		const click_ip *ip = reinterpret_cast<const click_ip *>(p->data() + sizeof(click_ether));
		p->set_ip_header(ip, ip->ip_hl << 2);
	}
	else if (vlan->ether_vlan_proto == _ethertype_ip6) {
		const click_ip6 *ip6 = reinterpret_cast<const click_ip6 *>(p->data() + sizeof(click_ether_vlan));
		p->set_ip6_header(ip6, 10 << 2);
	}

	return p;
}

#if HAVE_BATCH
PacketBatch *
AutoMarkIPHeader::simple_action_batch(PacketBatch *batch)
{
	EXECUTE_FOR_EACH_PACKET(simple_action, batch);
	return batch;
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(AutoMarkIPHeader)
ELEMENT_MT_SAFE(AutoMarkIPHeader)
