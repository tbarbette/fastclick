/*
 * gtpencap.{cc,hh}
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
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/standard/alignmentinfo.hh>
#include "gtpencap.hh"


CLICK_DECLS

GTPEncap::GTPEncap()
    : _eid(0)
{
}

GTPEncap::~GTPEncap()
{
}

int
GTPEncap::configure(Vector<String> &conf, ErrorHandler *errh)
{
    uint32_t eid;

    if (Args(conf, this, errh)
	.read_mp("TEID", eid)
	.complete() < 0)
	return -1;

    _eid = eid;

    return 0;
}

Packet *
GTPEncap::simple_action(Packet *p_in)
{
  WritablePacket *p = p_in->push(sizeof(click_gtp));
  click_gtp *gtp = reinterpret_cast<click_gtp *>(p->data());
  gtp->gtp_v = 1;
  gtp->gtp_pt = 1;
  gtp->gtp_reserved = 0;
  gtp->gtp_flags = 0;
  gtp->gtp_msg_type = 0xff;
  gtp->gtp_msg_len = htons(p->length() - sizeof(click_gtp));
  if (_eid == 0) {
      gtp->gtp_teid = htonl(AGGREGATE_ANNO(p));
  } else {
      gtp->gtp_teid = htonl(_eid);
  }

  return p;
}

#if HAVE_BATCH
PacketBatch*
GTPEncap::simple_action_batch(PacketBatch* batch) {
	EXECUTE_FOR_EACH_PACKET(GTPEncap::simple_action,batch);
	return batch;
}
#endif

String GTPEncap::read_handler(Element *e, void *thunk)
{
    GTPEncap *u = static_cast<GTPEncap *>(e);
    (void)u; //TODO
    switch ((uintptr_t) thunk) {
      default:
	return String();
    }
}

void GTPEncap::add_handlers()
{

}

CLICK_ENDDECLS
EXPORT_ELEMENT(GTPEncap)
ELEMENT_MT_SAFE(GTPEncap)
