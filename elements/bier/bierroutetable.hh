#ifndef CLICK_BIERROUTETABLE_HH
#define CLICK_BIERROUTETABLE_HH
#include <click/glue.hh>
#include <click/batchelement.hh>
#include <click/ip6address.hh>
#include <clicknet/bier.h>
CLICK_DECLS

class BierRouteTable : public BatchElement {
  public:
    void* cast(const char*) override;

    virtual int add_route(bfrid, bitstring, IP6Address, ErrorHandler*);
    virtual String dump_routes();

    static int add_route_handler(const String&, Element*, void*, ErrorHandler*);
    static String table_handler(Element*, void*);
};

CLICK_ENDDECLS
#endif
