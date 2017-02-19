#ifndef CLICK_AUTOMARKIPHeader_HH
#define CLICK_AUTOMARKIPHeader_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * AutoMarkIPHeader()
 * =s ip
 * sets IP header annotation
 * =d
 *
 * Marks packets as IP packets by setting the IP Header annotation. The packet is
 * assumed to be an ethernet or Vlan taged packet.
 * Does not check length fields for sanity, shorten packets to the IP length,
 * or set the destination IP address annotation. Use CheckIPHeader or
 * CheckIPHeader2 for that.
 *
 * =a CheckIPHeader, CheckIPHeader2, StripIPHeader, MarkIPHeader */

class AutoMarkIPHeader : public BatchElement {

	public:

		AutoMarkIPHeader() CLICK_COLD;
		~AutoMarkIPHeader() CLICK_COLD;

		const char *class_name() const		{ return "AutoMarkIPHeader"; }
		const char *port_count() const		{ return PORTS_1_1; }

		Packet      *simple_action(Packet *);
	#if HAVE_BATCH
		PacketBatch *simple_action_batch(PacketBatch *);
	#endif

	private:
		uint16_t _ethertype_8021q;
		uint16_t _ethertype_ip;
		uint16_t _ethertype_ip6;
};

CLICK_ENDDECLS
#endif
