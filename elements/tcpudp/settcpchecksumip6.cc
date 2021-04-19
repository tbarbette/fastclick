/*
 * settcpchecksumip6.{cc,hh} -- sets the TCP header checksum for IPv6 packets
 * Kristof Kovacs, based on settcpchecksum.{cc,hh} and fastudpsrcip6.{cc,hh}
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include "settcpchecksumip6.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip6.h>
#include <clicknet/tcp.h>

CLICK_DECLS

SetTCPChecksumIP6::SetTCPChecksumIP6()
{
}

SetTCPChecksumIP6::~SetTCPChecksumIP6()
{
}

int
SetTCPChecksumIP6::configure(Vector<String> &conf, ErrorHandler *errh)
{
  return Args(conf, this, errh)
        .complete();
}

Packet *
SetTCPChecksumIP6::simple_action(Packet *p_in)
{
  WritablePacket *p = p_in->uniqueify();
  click_ip6 *ip6 = p->ip6_header();
  click_tcp *tcp = p->tcp_header();
  unsigned plen = ntohs(ip6->ip6_plen);

  if (!p->has_transport_header() || plen < sizeof(click_tcp)
      || plen > (unsigned)p->transport_length())
    goto bad;

  //Only if TCP
  if (ip6->ip6_nxt == 6) {
      tcp->th_sum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, ip6->ip6_nxt, tcp->th_sum, (unsigned char *)tcp, ip6->ip6_plen));
  } else {
      //click_chatter("SetTCPChecksumIP6: Not a TCP/IPv6 packet");
  }

  return p;

 bad:
  click_chatter("SetTCPChecksumIP6: bad lengths");
  p->kill();
  return(0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(ip6)
EXPORT_ELEMENT(SetTCPChecksumIP6)
ELEMENT_MT_SAFE(SetTCPChecksumIP6)
