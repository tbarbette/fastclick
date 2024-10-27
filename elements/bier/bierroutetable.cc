/*
 * bierroutetable.{cc,hh} -- element encapsulates packet in IP6 SRv6 header
 * Nicolas Rybowski
 *
 * Copyright (c) 2024 UCLouvain
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

int BierRouteTable::add_route(bfrid, bitstring, IP6Address, int, String, ErrorHandler *errh) {
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
  int output;
  String ifname;

  int ok;

  // TODO: read_all for BFRID and define dst as an array for route addition batching
  ok = Args(words, r, errh)
       .read_mp("BFRID", dst)
       .read_mp("BITSTRING", fbm)
       .read_mp("NXT", nxt)
       .read_mp("IFIDX", output)
       .read_mp("IFNAME", ifname)
       .complete();

  if (ok >= 0)
    ok = r->add_route(dst, fbm, nxt, output, ifname, errh);

  return ok;
}

String BierRouteTable::table_handler(Element *e, void *) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  return r->dump_routes();
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(BierRouteTable)
