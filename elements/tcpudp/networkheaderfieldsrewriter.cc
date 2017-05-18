/*
 * networkheaderfieldsrewriter.{cc,hh} -- element rewrites selective header fields
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
#include "networkheaderfieldsrewriter.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ether.h>
#include <clicknet/ip.h>
#include <clicknet/ip6.h>
#include <clicknet/udp.h>
#include <clicknet/tcp.h>
CLICK_DECLS

NetworkHeaderFieldsRewriter::NetworkHeaderFieldsRewriter() :
	_eth_dst_set(false),
	_eth_src_set(false),
	_eth_type_set(false),
	_any_set(false),
	_any_ipv4_set(false),
	_ipv4_proto_set(false),
	_ipv4_src_set(false),
	_ipv4_dst_set(false),
	_ipv4_dscp_set(false),
	_ipv4_ttl_set(false),
	_ipv4_ecn_set(false),
	_tcp_src_set(false),
	_tcp_dst_set(false),
	_udp_src_set(false),
	_udp_dst_set(false),
	_ethertype_8021q(htons(ETHERTYPE_8021Q)),
	_ethertype_ip(htons(ETHERTYPE_IP)),
	_ethertype_ip6(htons(ETHERTYPE_IP6))
{
}

NetworkHeaderFieldsRewriter::~NetworkHeaderFieldsRewriter()
{
}

int
NetworkHeaderFieldsRewriter::configure(Vector<String> &conf, ErrorHandler *errh)
{
	unsigned dscp_val = 0;
	unsigned ecn_val = 0;
	if (Args(conf, this, errh)
	.read("ETH_SRC", _eth_src).read_status(_eth_src_set)
	.read("ETH_DST", _eth_dst).read_status(_eth_dst_set)
	.read("ETH_TYPE", _eth_type).read_status(_eth_type_set)
	.read("IPV4_PROTO", _ipv4_proto).read_status(_ipv4_proto_set)
	.read("IPV4_SRC", _ipv4_src).read_status(_ipv4_src_set)
	.read("IPV4_DST", _ipv4_dst).read_status(_ipv4_dst_set)
	.read("IPV4_DSCP", dscp_val).read_status(_ipv4_dscp_set)
	.read("IPV4_TTL", _ipv4_ttl).read_status(_ipv4_ttl_set)
	.read("IPV4_ECN", ecn_val).read_status(_ipv4_ecn_set)
	.read("TCP_SRC", _tcp_src).read_status(_tcp_src_set)
	.read("TCP_DST", _tcp_dst).read_status(_tcp_dst_set)
	.read("UDP_SRC", _udp_src).read_status(_udp_src_set)
	.read("UDP_DST", _udp_dst).read_status(_udp_dst_set)
	.complete() < 0) {
		return -1;
	}
	if (ecn_val > 3) {
		return errh->error("ECN out of range");
	}
	if (dscp_val > 0x3f) {
		return errh->error("diffserv code point out of range");
	}
	_ipv4_dscp = (dscp_val << 2);
	_ipv4_ecn = ecn_val;
	_any_ipv4_set = _ipv4_proto_set || _ipv4_src_set || _ipv4_dst_set || _ipv4_dscp_set || _ipv4_ttl_set || _ipv4_ecn_set;
	_any_tcp_set = _tcp_dst_set || _tcp_src_set;
	_any_udp_set = _udp_dst_set || _udp_src_set;
	_any_set = _eth_src_set || _eth_dst_set || _eth_type_set || _any_ipv4_set || _any_udp_set || _any_tcp_set;
	return 0;
}

Packet *
NetworkHeaderFieldsRewriter::simple_action(Packet *p)
{
	if (!_any_set) {
		return p;
	}

	WritablePacket *q = p->uniqueify();
	if (!q) {
		return 0;
	}

	if (_eth_dst_set) {
		memcpy(q->data(), _eth_dst.data(), 6);
	}

	if (_eth_src_set) {
		memcpy(q->data() + 6, _eth_src.data(), 6);
	}

	click_ether_vlan *ethh = reinterpret_cast<click_ether_vlan *>(q->data());
	if (_eth_type_set) {
		if (ethh->ether_vlan_proto == _ethertype_8021q) {
			ethh->ether_vlan_encap_proto = htons(_eth_type);
		} else {
			ethh->ether_vlan_proto = htons(_eth_type);
		}
	}

	bool ipv4_layer_present = ((ethh->ether_vlan_proto == _ethertype_ip)
			|| ((ethh->ether_vlan_proto == _ethertype_8021q &&
					ethh->ether_vlan_encap_proto == _ethertype_ip)));
	bool ipv6_layer_present = ((ethh->ether_vlan_proto == _ethertype_ip6) ||
				((ethh->ether_vlan_proto == _ethertype_8021q &&
					ethh->ether_vlan_encap_proto == _ethertype_ip6)));
	if (ipv4_layer_present && q->has_network_header()) {
		click_ip *iph = q->ip_header();
		unsigned char * neth = q->network_header();
		iph->ip_p = _ipv4_proto_set ? _ipv4_proto : iph->ip_p;
		iph->ip_ttl = _ipv4_ttl_set ? _ipv4_ttl : iph->ip_ttl;
		iph->ip_tos = _ipv4_dscp_set ? ((iph->ip_tos & 0x3) | _ipv4_dscp) : iph->ip_tos;
		iph->ip_tos = _ipv4_ecn_set ? (iph->ip_tos & IP_DSCPMASK) | _ipv4_ecn : iph->ip_tos;
		if (_ipv4_src_set) {
			memcpy(neth + 12, &_ipv4_src, 4);
		}
		if (_ipv4_dst_set) {
			memcpy(neth + 16, &_ipv4_dst, 4);
		}

		if (_any_tcp_set && iph->ip_p == IP_PROTO_TCP && IP_FIRSTFRAG(iph) && (int)q->length() >= q->transport_header_offset() + 8) {
			click_tcp *tcph = q->tcp_header();
			tcph->th_sport = _tcp_src_set ? htons(_tcp_src) : tcph->th_sport;
			tcph->th_dport = _tcp_dst_set ? htons(_tcp_dst) : tcph->th_dport;
		}
		if (_any_udp_set && iph->ip_p == IP_PROTO_UDP && IP_FIRSTFRAG(iph) && (int)q->length() >= q->transport_header_offset() + 8) {
			click_udp *udph = q->udp_header();
			udph->uh_sport = _udp_src_set ? htons(_udp_src) : udph->uh_sport;
			udph->uh_dport = _udp_dst_set ? htons(_udp_dst) : udph->uh_dport;
		}
	}
	if (ipv6_layer_present && q->has_network_header()) {
		click_ip6 *iph = q->ip6_header();
		if (_any_tcp_set && iph->ip6_nxt == IP_PROTO_TCP && (int)q->length() >= q->transport_header_offset() + 8) {
			click_tcp *tcph = q->tcp_header();
			tcph->th_sport = _tcp_src_set ? htons(_tcp_src) : tcph->th_sport;
			tcph->th_dport = _tcp_dst_set ? htons(_tcp_dst) : tcph->th_dport;
		}
		if (_any_udp_set && iph->ip6_nxt == IP_PROTO_UDP  && (int)q->length() >= q->transport_header_offset() + 8) {
			click_udp *udph = q->udp_header();
			udph->uh_sport = _udp_src_set ? htons(_udp_src) : udph->uh_sport;
			udph->uh_dport = _udp_dst_set ? htons(_udp_dst) : udph->uh_dport;
		}
	}

	return q;
}

#if HAVE_BATCH
PacketBatch *
NetworkHeaderFieldsRewriter::simple_action_batch(PacketBatch *batch)
{
	EXECUTE_FOR_EACH_PACKET(simple_action, batch);
	return batch;
}
#endif

void
NetworkHeaderFieldsRewriter::add_handlers()
{
	add_read_handler("eth_src", read_keyword_handler, "ETH_SRC");
	add_write_handler("eth_src", reconfigure_keyword_handler, "ETH_SRC");
	add_read_handler("eth_dst", read_keyword_handler, "ETH_DST");
	add_write_handler("eth_dst", reconfigure_keyword_handler, "ETH_DST");
	add_read_handler("eth_type", read_keyword_handler, "ETH_TYPE");
	add_write_handler("eth_type", reconfigure_keyword_handler, "ETH_TYPE");
	add_read_handler("ipv4_proto", read_keyword_handler, "IPV4_PROTO");
	add_write_handler("ipv4_proto", reconfigure_keyword_handler, "IPV4_PROTO");
	add_read_handler("ipv4_ttl", read_keyword_handler, "IPV4_TTL");
	add_write_handler("ipv4_ttl", reconfigure_keyword_handler, "IPV4_TTL");
	add_read_handler("ipv4_dscp", read_keyword_handler, "IPV4_DSCP");
	add_write_handler("ipv4_dscp", reconfigure_keyword_handler, "IPV4_DSCP");
	add_read_handler("ipv4_src", read_keyword_handler, "IPV4_SRC");
	add_write_handler("ipv4_src", reconfigure_keyword_handler, "IPV4_SRC");
	add_read_handler("ipv4_dst", read_keyword_handler, "IPV4_DST");
	add_write_handler("ipv4_dst", reconfigure_keyword_handler, "IPV4_DST");
	add_read_handler("ipv4_ecn", read_keyword_handler, "IPV4_ECN");
	add_write_handler("ipv4_ecn", reconfigure_keyword_handler, "IPV4_ECN");
	add_read_handler("tcp_src", read_keyword_handler, "TCP_SRC");
	add_write_handler("tcp_src", reconfigure_keyword_handler, "TCP_SRC");
	add_read_handler("tcp_dst", read_keyword_handler, "TCP_DST");
	add_write_handler("tcp_dst", reconfigure_keyword_handler, "TCP_DST");
	add_read_handler("udp_src", read_keyword_handler, "UDP_SRC");
	add_write_handler("udp_src", reconfigure_keyword_handler, "UDP_SRC");
	add_read_handler("udp_dst", read_keyword_handler, "UDP_DST");
	add_write_handler("udp_dst", reconfigure_keyword_handler, "UDP_DST");
}

CLICK_ENDDECLS
EXPORT_ELEMENT(NetworkHeaderFieldsRewriter)
