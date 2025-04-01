/*
 * bierroutetable.{cc,hh} -- implement handlers for BIFT element
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
#include <click/ipaddress.hh>
CLICK_DECLS

void* BierRouteTable::cast(const char *name) {
  if (strcmp(name, "BierRouteTable") == 0)
    return (void*) this;
  else
    return Element::cast(name);
}

int BierRouteTable::add_route(bfrid, IP6Address, bitstring, IP6Address, int, String, ErrorHandler *errh) {
  return errh->error("cannot add routes to this routing table");
}

int BierRouteTable::del_route(bfrid, ErrorHandler *errh) {
  return errh->error("cannot del routes to this routing table");
}

String BierRouteTable::dump_routes() {
  return String();
}

int BierRouteTable::add_route_handler(const String &conf, Element *e, void *, ErrorHandler *errh) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  Vector<String> words;
  cp_spacevec(conf, words);

  bitstring fbm;
  IP6Address nxt;
  int output;
  String ifname;
  String raw_bfrs;

  if (
    Args(words, r, errh)
       .read_mp("BITSTRING", fbm)
       .read_mp("NXT", nxt)
       .read_mp("IFIDX", output)
       .read_mp("IFNAME", ifname)
       .read_mp("BFR", raw_bfrs)
       .complete() < 0
  )
    return -1;

  Vector<String> bfrs;
  cp_argvec(raw_bfrs, bfrs);

  for (int i=0; i<bfrs.size(); i++) {
    Vector<String> bfr = bfrs[i].split('_');
    if (bfr.size() != 2) {
      click_chatter("Invalid BFR specification. Should be \"BFR <bfr-id>_<bfr-prefix>\"");
      continue;
    }
    if (r->add_route((bfrid) atoi(bfr[0].c_str()), IP6Address(bfr[1]), fbm, nxt, output, ifname, errh)) {
      click_chatter("Failed to add BIFT entry. Ignoring.");
      continue;
    }
  }

  return 0;
}

String BierRouteTable::table_handler(Element *e, void *) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  return r->dump_routes();
}

int BierRouteTable::del_route_handler(const String &conf, Element *e, void *, ErrorHandler *errh) {
  BierRouteTable *r = static_cast<BierRouteTable *>(e);
  Vector<String> words;
  cp_spacevec(conf, words);

  bfrid bfrid;

  if (Args(words, r, errh).read_mp("BFR-ID", bfrid).complete() < 0)
    return -1;

  return r->del_route(bfrid, errh);

}

CLICK_ENDDECLS
ELEMENT_PROVIDES(BierRouteTable)
