#ifndef CLICK_BIFT_HH
#define CLICK_BIFT_HH
#include <click/batchelement.hh>
#include <click/ip6address.hh>
#include <click/biertable.hh>
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

		void add_handlers() override CLICK_COLD;

		int add_route(bfrid dst, bitstring fbm, IP6Address nxt, ErrorHandler*);
		String dump_routes() { return _t.dump(); }

	private:
		BierTable _t;
};

CLICK_ENDDECLS
#endif
