#ifndef CLICK_NETWORKHEADERFIELDSREWRITER_HH
#define CLICK_NETWORKHEADERFIELDSREWRITER_HH
#include <click/batchelement.hh>
#include <click/etheraddress.hh>
CLICK_DECLS

/*
=c
NetworkHeaderFieldsRewriter([ETHERNET, IPV4, IPV6, TCP, UDP])
=s basicmod
Rewrites p

=d
Incoming packets are Ethernet. Selected fields (given by keywords) will be rewritten.
Keyword arguments are:
=over 8

=item ETH_SRC

Ethernet source address.

=item ETH_DST

Ethernet destination address.

=item ETH_TYPE

The layer above ethernet (work with VLAN tags).

=item IPV4_SRC

IPv4 source address

=item IPV4_DST

IPv4 destination address

=item IPV4_PROTO

IPv4 next protocol

=item IPV4_DSCP

IPv4 DSCP value

=item IPV4_TTL

IPv4 TTL value.

=item IPV4_ECN

IPv4 ECN value

=item TCP_SRC

TCP source port (works over IPv6 as well)

=item TCP_DST

TCP destination port (works over IPv6 as well)

=item UDP_SRC

UDP source port (works over IPv6 as well)

=item UDP_DST

UDP destination port (works over IPv6 as well)

=back
 * */

class NetworkHeaderFieldsRewriter : public BatchElement {
	public:

		NetworkHeaderFieldsRewriter() CLICK_COLD;
		~NetworkHeaderFieldsRewriter() CLICK_COLD;

		const char *class_name() const	{ return "NetworkHeaderFieldsRewriter"; }
		const char *port_count() const	{ return PORTS_1_1; }

		int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
		bool can_live_reconfigure() const { return true; }
		void add_handlers() CLICK_COLD;

		Packet      *simple_action      (Packet *);
	#if HAVE_BATCH
		PacketBatch *simple_action_batch(PacketBatch *);
	#endif

	private:
		bool _eth_dst_set, _eth_src_set, _eth_type_set;
		bool _ipv4_proto_set, _ipv4_src_set, _ipv4_dst_set, _ipv4_dscp_set, _ipv4_ttl_set, _ipv4_ecn_set;
		bool _tcp_src_set, _tcp_dst_set, _udp_src_set, _udp_dst_set;
		bool _any_ipv4_set, _any_set, _any_tcp_set, _any_udp_set;
		EtherAddress _eth_dst, _eth_src;
		uint16_t _eth_type, _udp_src, _udp_dst, _tcp_src, _tcp_dst;
		uint8_t _ipv4_proto, _ipv4_ttl, _ipv4_dscp, _ipv4_ecn;
		IPAddress _ipv4_src, _ipv4_dst;

		uint16_t _ethertype_8021q;
		uint16_t _ethertype_ip;
		uint16_t _ethertype_ip6;
};

CLICK_ENDDECLS
#endif // CLICK_NETWORKHEADERFIELDSREWRITER_HH
