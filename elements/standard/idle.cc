/*
 * idle.{cc,hh} -- element just sits there and kills packets
 * Robert Morris, Eddie Kohler
 *
 * Computational batching support by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2017 KTH Royal Institute of Technology
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
#include "idle.hh"
CLICK_DECLS

Idle::Idle()
    : _notifier(NotifierSignal::idle_signal())
{
    in_batch_mode = BATCH_MODE_YES;
}

void *
Idle::cast(const char *name)
{
  if (strcmp(name, Notifier::EMPTY_NOTIFIER) == 0)
    return &_notifier;
  else
    return Element::cast(name);
}

void
Idle::push(int, Packet *p)
{
  p->kill();
}

#if HAVE_BATCH
void
Idle::push_batch(int, PacketBatch *batch)
{
  batch->fast_kill();
}
#endif

Packet *
Idle::pull(int)
{
  return 0;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Idle)
ELEMENT_MT_SAFE(Idle)
