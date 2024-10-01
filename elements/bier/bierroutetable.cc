#include <click/config.h>
#include "bierroutetable.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/ip6address.hh>
CLICK_DECLS

void* BierRouteTable::cast(const char *name) {
  if (strcmp(name, "BierRouteTable") == 0)
    return (void*) this;
  else
    return Element::cast(name);
}

int BierRouteTable::add_route(bfrid, bitstring, IP6Address, ErrorHandler *errh) {
  return errh->error("cannot add routes to this routing table");
}

String BierRouteTable::dump_routes() {
  return String();
}

int BierRouteTable::add_route_handler(const String &conf, Element *e, void *, ErrorHandler *errh) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  Vector<String> words;
  cp_spacevec(conf, words);

  bfrid dst;
  bitstring fbm;
  IP6Address nxt;

  int ok;

  ok = Args(words, r, errh)
       .read_mp("BFRID", dst)
       .read_mp("BITSTRING", fbm)
       .read_mp("NXT", nxt)
       .complete();

  if (ok >= 0)
    ok = r->add_route(dst, fbm, nxt, errh);

  return ok;
}

String BierRouteTable::table_handler(Element *e, void *) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  return r->dump_routes();
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(BierRouteTable)
