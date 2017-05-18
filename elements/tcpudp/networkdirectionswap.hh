#ifndef CLICK_NETWORKDIRECTIONSWAP_HH
#define CLICK_NETWORKDIRECTIONSWAP_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * NetworkDirectionSwap([ETHERNET, IPV4, IPV6, TCP, UDP])
 * =s basicmod
 * swaps network direction of packet
 *
 * =d
 *
 * Incoming packets are Ethernet. The source and address of requested layers is swapped.
 * Keyword arguments are:
=over 8

=item ETHERNET

Boolean. If true ethernet layer will swap direction.

=item IPV4

Boolean. If true IPv4 layer will swap direction.

=item IPV6

Boolean. If true IPv6 layer will swap direction.

=item TCP

Boolean. If true TCP layer will swap direction.

=item UDP

Boolean. If true UDP layer will swap direction.

=back
=e
 * */

class NetworkDirectionSwap : public BatchElement {
	public:

		NetworkDirectionSwap() CLICK_COLD;
		~NetworkDirectionSwap() CLICK_COLD;

		const char *class_name() const	{ return "NetworkDirectionSwap"; }
		const char *port_count() const	{ return PORTS_1_1; }

		int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
		bool can_live_reconfigure() const { return true; }
		void add_handlers() CLICK_COLD;

		Packet      *simple_action      (Packet *);
	#if HAVE_BATCH
		PacketBatch *simple_action_batch(PacketBatch *);
	#endif

	private:
		bool _ethernet;
		bool _ipv4;
		bool _ipv6;
		bool _tcp;
		bool _udp;
		bool _swap_any;
		uint16_t _ethertype_8021q;
		uint16_t _ethertype_ip;
		uint16_t _ethertype_ip6;
};

CLICK_ENDDECLS
#endif // CLICK_NETWORKDIRECTIONSWAP_HH
