#ifndef CLICK_BIFT_HH
#define CLICK_BIFT_HH
#include <click/batchelement.hh>
#include <click/ip6address.hh>
#include <click/biertable.hh>
#include <click/hashmap.hh>
#include "bierroutetable.hh"
CLICK_DECLS

/*
 * =c
 * LookupBierTable(BIFT_ID BFR_ID IFACE ...)
 * =s bier
 *
 * =d
 * Input: IP6 packets (no ether header).
 * Expects a destination IP6 address annotation with each packet.
 *
 * Implements the forwarding procedure of BIER packets as defined in RFC8279 Section 6.
 * Outputs 0 and 1 are reserved: discarded packets are pushed on output 0 while packets destined
 * to the current BFR are pushed on output 1.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item BIFT_ID
 *
 * The identifier of the current BIFT. This identifier defines the bitstring length (BSL),
 * set identifier (SI) and sub-domain identifier (SD).
 *
 * =item BFR_ID
 *
 * The identifier of the current BFR.
 *
 * =item IFACE
 *
 * A mapping of a physical interface with the output number of the element.
 *
 * =back
 *
 * =e
 *
 *   bift :: LookupBierTable(BIFT_ID 0x01234 BFR_ID 1 IFACE eth0:2 IFACE eth1:3);
 *   rt[0] -> Discard;
 *   rt[1] -> ... -> ToDevice(lo);
 *   rt[2] -> ... -> ToDevice(eth0);
 *   rt[3] -> ... -> ToDevice(eth1);
 *   ...
 *
 */

class LookupBierTable : public ClassifyElement<LookupBierTable, BierRouteTable> {
	public:
		LookupBierTable();
		~LookupBierTable();

		const char *class_name() const override { return "LookupBierTable"; }
		const char *port_count() const override { return "1/-"; }
		const char *processing() const override { return PUSH; }

		int classify(Packet *p);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		void add_handlers() override CLICK_COLD;

		int add_route(bfrid dst, IP6Address bfr_prefix, bitstring fbm, IP6Address nxt, int, String, ErrorHandler*);
		int del_route(bfrid, ErrorHandler*);
		String dump_routes() { return _t.dump(); }

	private:
		BierTable _t;

    uint16_t _bfr_id;
    uint32_t _bift_id;
    atomic_uint64_t _drops;
    HashMap<String, int> _ifaces;

    void drop(Packet *p);
    void decap(click_bier *bier);
};

CLICK_ENDDECLS
#endif
