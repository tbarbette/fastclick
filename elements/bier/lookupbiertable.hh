#ifndef CLICK_BIFT_HH
#define CLICK_BIFT_HH
#include <click/batchelement.hh>
#include <click/ip6address.hh>
#include <click/biertable.hh>
#include <click/hashmap.hh>
#include "bierroutetable.hh"
CLICK_DECLS

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

		int add_route(bfrid dst, bitstring fbm, IP6Address nxt, int, String, ErrorHandler*);
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
