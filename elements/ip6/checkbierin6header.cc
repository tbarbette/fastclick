/*
 * checkbierin6header.{cc,hh} -- element encapsulates packet in IP6 SRv6 header
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
#include "checkbierin6header.hh"
#include <clicknet/ip6.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/bier.h>
#include <click/error.hh>

CLICK_DECLS

CheckBIERin6Header::CheckBIERin6Header(): _offset(0) {
  _drops = 0;
}

CheckBIERin6Header::~CheckBIERin6Header() {}

int CheckBIERin6Header::configure(Vector<String> &conf, ErrorHandler *errh) {
  if (Args(conf, this, errh)
    .read_p("OFFSET", _offset)
    .complete() < 0
  )
    return -1;
  return 0;
}

void CheckBIERin6Header::drop(Packet *p) {
  if (_drops == 0) {
    click_chatter("IPv6 packet does not contains a BIER packet");
  }
  _drops++;

  if (noutputs() == 2) {
    output(1).push(p);
  } else {
    p->kill();
  }
}

Packet *CheckBIERin6Header::simple_action(Packet *p) {
  
  const click_ip6 *iph = (click_ip6*) p->ip_header();
  if (iph->ip6_nxt != IP6PROTO_BIERIN6) {
    click_chatter("Not a BIERin6 packet");
    goto drop;
  }

  return(p);
  
  drop:
    drop(p);
    return 0;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(CheckBIERin6Header)
ELEMENT_MT_SAFE(CheckBIERin6Header)
