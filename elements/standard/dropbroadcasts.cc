/*
 * dropbroadcasts.{cc,hh} -- element that drops broadcast packets
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2016 KTH Royal Institute of Technology
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
#include "dropbroadcasts.hh"
#include <click/glue.hh>
CLICK_DECLS

DropBroadcasts::DropBroadcasts()
{
  _drops = 0;
}

void
DropBroadcasts::drop_it(Packet *p)
{
  if (_drops == 0)
    click_chatter("DropBroadcasts: dropped a packet");
  _drops++;
  if (noutputs() == 2)
    output(1).push(p);
  else
    p->kill();
}

Packet *
DropBroadcasts::simple_action(Packet *p)
{
  if (p->packet_type_anno() == Packet::BROADCAST || p->packet_type_anno() == Packet::MULTICAST) {
    drop_it(p);
    return 0;
  } else
    return p;
}

#if HAVE_BATCH
PacketBatch *
DropBroadcasts::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(simple_action, batch, [](Packet *p){});
    return batch;
}
#endif

static String
dropbroadcasts_read_drops(Element *f, void *)
{
  DropBroadcasts *q = (DropBroadcasts *)f;
  return String(q->drops());
}

void
DropBroadcasts::add_handlers()
{
  add_read_handler("drops", dropbroadcasts_read_drops, 0);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DropBroadcasts)
ELEMENT_MT_SAFE(DropBroadcasts)
