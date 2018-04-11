/*
 * gtpdecap.{cc,hh}
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
#include "gtpdecap.hh"
CLICK_DECLS

GTPDecap::GTPDecap() : _anno(true)
{
}

GTPDecap::~GTPDecap()
{
}

int
GTPDecap::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.complete() < 0)
	return -1;

    return 0;
}

Packet *
GTPDecap::simple_action(Packet *p)
{
  const click_gtp *gtp = reinterpret_cast<const click_gtp *>(p->data());
  int sz = sizeof(click_gtp);
  if (_anno)
      SET_AGGREGATE_ANNO(p, ntohl(gtp->gtp_teid));
  if (gtp->gtp_flags)
      sz += 4;
  p->pull(sz);

  return p;
}

#if HAVE_BATCH
PacketBatch*
GTPDecap::simple_action_batch(PacketBatch* batch) {
	FOR_EACH_PACKET(batch, p)
        GTPDecap::simple_action(p);
	return batch;
}
#endif


CLICK_ENDDECLS
EXPORT_ELEMENT(GTPDecap)
ELEMENT_MT_SAFE(GTPDecap)
