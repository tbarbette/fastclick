/*
 * SetICMPChecksumIP6.{cc,hh} -- sets the ICMPv6 header checksum for IPv6 packets
 * Tom Barbette, based on settcpchecksum6.{cc,hh}
 *
 * Copyright (c) 2021 UCLouvain
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
#include "seticmpchecksumip6.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/ip6address.hh>
#include <clicknet/icmp.h>

CLICK_DECLS

SetICMPChecksumIP6::SetICMPChecksumIP6()
{
}

SetICMPChecksumIP6::~SetICMPChecksumIP6()
{
}

int
SetICMPChecksumIP6::configure(Vector<String> &conf, ErrorHandler *errh)
{
  return Args(conf, this, errh)
        .complete();
}

Packet *
SetICMPChecksumIP6::simple_action(Packet *p_in)
{
  WritablePacket *p = p_in->uniqueify();
  click_ip6 *ip6 = p->ip6_header();
  unsigned short icmp_len;
  click_icmp *icmp =  (click_icmp*)ip6_find_header(ip6, IP_PROTO_ICMP6 , p->end_data());
  if (icmp == 0) {
    goto bad;
  }


  icmp_len = htons(p->end_data() - p->transport_header());
  icmp->icmp_cksum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, icmp_len, IP_PROTO_ICMP6, icmp->icmp_cksum, (unsigned char *)icmp, icmp_len));

  return p;

 bad:
  click_chatter("SetICMPChecksumIP6: cannot find ICMPv6 header");
  p->kill();
  return(0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(ip6)
EXPORT_ELEMENT(SetICMPChecksumIP6)
ELEMENT_MT_SAFE(SetICMPChecksumIP6)
