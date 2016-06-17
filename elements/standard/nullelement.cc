/*
 * nullelement.{cc,hh} -- do-nothing element
 * Eddie Kohler
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
#include "nullelement.hh"
CLICK_DECLS

NullElement::NullElement()
{
}

Packet *
NullElement::simple_action(Packet *p)
{
  return p;
}

#if HAVE_BATCH
PacketBatch*
NullElement::simple_action_batch(PacketBatch *batch)
{
    EXECUTE_FOR_EACH_PACKET(simple_action, batch);
    return batch;
}
#endif

PushNullElement::PushNullElement()
{
}

void
PushNullElement::push_packet(int, Packet *p)
{
  output(0).push(p);
}
#if HAVE_BATCH
void
PushNullElement::push_batch(int, PacketBatch *batch)
{
    output_push_batch(0, batch);
}
#endif

#if HAVE_BATCH
static const short PULL_LIMIT = 32;
#endif

PullNullElement::PullNullElement()
{
}

Packet *
PullNullElement::pull(int)
{
  return input(0).pull();
}

#if HAVE_BATCH
PacketBatch *
PullNullElement::pull_batch(int)
{
    return input_pull_batch(0, PULL_LIMIT);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(NullElement)
EXPORT_ELEMENT(PushNullElement)
EXPORT_ELEMENT(PullNullElement)
ELEMENT_MT_SAFE(NullElement)
ELEMENT_MT_SAFE(PushNullElement)
ELEMENT_MT_SAFE(PullNullElement)
