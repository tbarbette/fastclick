/*
 * SetTransportChecksumIP6.{cc,hh} -- sets the transport header checksum for IPv6 packets
 * Based on Kristof Kovacs, based on settcpchecksum.{cc,hh} and fastudpsrcip6.{cc,hh}
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
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
#include "settransportchecksumip6.hh"
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/ip6address.hh>
#include <clicknet/ip6.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>
#include <clicknet/icmp.h>

CLICK_DECLS

SetTransportChecksumIP6::SetTransportChecksumIP6()
{
}

SetTransportChecksumIP6::~SetTransportChecksumIP6()
{
}

int
SetTransportChecksumIP6::configure(Vector<String> &conf, ErrorHandler *errh)
{
  return Args(conf, this, errh)
        .complete();
}

Packet *
SetTransportChecksumIP6::simple_action(Packet *p_in)
{
  WritablePacket *p = p_in->uniqueify();
  click_ip6 *ip6 = p->ip6_header();

  unsigned plen = ntohs(ip6->ip6_plen);

  if (unlikely(!p->has_transport_header() || !IP6_NXT_ANNO(p))) {
    click_chatter("%p{element}: needs the transport header and IP6_NXT annotation to be set, use CheckIP6Header", this);
    p->kill();
    return 0;
  }

  switch (IP6_NXT_ANNO(p)) {
    case IP_PROTO_TCP: {
      click_tcp *tcp = (click_tcp *)p->transport_header();

      if (unlikely(!p->has_transport_header() || plen < sizeof(click_tcp))) {
        click_chatter("SetTransportChecksumIP6: bad lengths (got %d that should be higher than %d", plen, sizeof(click_tcp));
        p->kill();
        return(0);
      }

      tcp->th_sum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, IP6_NXT_ANNO(p), tcp->th_sum, (unsigned char *)tcp, ip6->ip6_plen));
      break;
    }
    case IP_PROTO_UDP: {
      click_udp *udp = (click_udp *)p->transport_header();
      if (unlikely(!p->has_transport_header() || plen < sizeof(click_udp))) {
        click_chatter("SetTransportChecksumIP6: bad lengths (got %d that should be higher than %d", plen, sizeof(click_udp));
        p->kill();
        return(0);
      }
      udp->uh_sum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, IP6_NXT_ANNO(p), udp->uh_sum, (unsigned char *)udp, ip6->ip6_plen));
      break;
    }
    case IP_PROTO_ICMP6: {
      click_icmp *icmp = (click_icmp *)p->transport_header();
      if (unlikely(!p->has_transport_header() || plen < sizeof(click_icmp))) {
        click_chatter("SetTransportChecksumIP6: bad lengths (got %d that should be higher than %d", plen, sizeof(click_icmp));
        p->kill();
        return(0);
      }
      icmp->icmp_cksum = htons(in6_fast_cksum(&ip6->ip6_src, &ip6->ip6_dst, ip6->ip6_plen, IP6_NXT_ANNO(p), icmp->icmp_cksum, (unsigned char *)icmp, ip6->ip6_plen));
      break;
    }
  }

  return p;
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(ip6)
EXPORT_ELEMENT(SetTransportChecksumIP6)
EXPORT_ELEMENT(SetTransportChecksumIP6-SetTCPChecksumIP6)
EXPORT_ELEMENT(SetTransportChecksumIP6-SetUDPChecksumIP6)
EXPORT_ELEMENT(SetTransportChecksumIP6-SetICMPChecksumIP6)
ELEMENT_MT_SAFE(SetTransportChecksumIP6)
