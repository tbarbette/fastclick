#include <click/config.h>
#include "lookupbiertable.hh"
#include "bierroutetable.hh"
CLICK_DECLS

LookupBierTable::LookupBierTable() {}
LookupBierTable::~LookupBierTable() {}

int LookupBierTable::classify(Packet *p) { return 0; }

int LookupBierTable::add_route(bfrid dst, bitstring fbm, IP6Address nxt, ErrorHandler *errh) {
  // if (output < 0 && output >= noutputs())
  // return errh->error("port number out of range");
  _t.add(dst, fbm, nxt);
  return 0;
}

void LookupBierTable::add_handlers() {
  add_write_handler("add", add_route_handler, 0);
  add_read_handler("table", table_handler, 0);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(BierRouteTable)
EXPORT_ELEMENT(LookupBierTable)
