/*
 * gtpfilter.{cc,hh} -- GTP Filtering element
 * Tom Barbette
 *
 * Copyright (c) 2018 University of Liege
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
#include <clicknet/ip.h>
#include <clicknet/udp.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include "gtpfilter.hh"

CLICK_DECLS

GTPFilter::GTPFilter()
{
}

GTPFilter::~GTPFilter()
{
}

int
GTPFilter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.complete() < 0)
	return -1;

    return 0;
}

Packet *
GTPFilter::simple_action(Packet *p_in)
{
  Packet* p = p_in;
  const click_ip *ip = reinterpret_cast<const click_ip *>(p->data());
  const click_udp *udp = reinterpret_cast<const click_udp *>(p->data() + sizeof(click_ip));
  const click_gtp *gtp = reinterpret_cast<const click_gtp *>(p->data() + sizeof(click_ip) + sizeof(click_udp));
  //GTPTuple gtp_id(IP6Address(ip->ip_src), IPAddress(ip->ip_dst), ntohs(udp->uh_sport), ntohl(gtp->gtp_teid));


  return p;
}

#if HAVE_BATCH
PacketBatch*
GTPFilter::simple_action_batch(PacketBatch* batch) {
	EXECUTE_FOR_EACH_PACKET(GTPFilter::simple_action,batch);
	return batch;
}
#endif

String GTPFilter::read_handler(Element *e, void *thunk)
{
    GTPFilter *u = static_cast<GTPFilter *>(e);
    switch ((uintptr_t) thunk) {
      default:
	return String();
    }
}

void GTPFilter::add_handlers()
{

}

CLICK_ENDDECLS
EXPORT_ELEMENT(GTPFilter)
ELEMENT_MT_SAFE(GTPFilter)
