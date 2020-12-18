/*
 * checklength.{cc,hh} -- element checks packet length
 * Eddie Kohler
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
#include "checklength.hh"
#include <click/args.hh>
#include <click/error.hh>
CLICK_DECLS

CheckLength::CheckLength()
{
}

int
CheckLength::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return Args(conf, this, errh)
                .read_mp("LENGTH", _max)
                .read_or_set("EXTRA_LENGTH", _use_extra_length, false)
                .complete();
}

Packet*
CheckLength::pull(int) {
    Packet* p = input(0).pull();
    if (p && (p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0)) > _max) {
      checked_output_push(1, p);
      return 0;
    } else
      return p;
}

void
CheckLength::push(int, Packet *p)
{
  if (p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0) > _max)
    checked_output_push(1, p);
  else
    output(0).push(p);
}

#if HAVE_BATCH
PacketBatch *
CheckLength::pull_batch(int,unsigned max)
{
  PacketBatch *batch = input(0).pull_batch(max);
    auto fn = [this](Packet*p){
      if (p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0) > _max)
        return 1;
      else
        return 0;
    };
  CLASSIFY_EACH_PACKET(2,fn,batch,checked_output_push);
}

void
CheckLength::push_batch(int, PacketBatch *batch)
{
    auto fn = [this](Packet*p){
      if (p->length() + (_use_extra_length ? EXTRA_LENGTH_ANNO(p) : 0) > _max)
        return 1;
      else
        return 0;
    };
    CLASSIFY_EACH_PACKET(2,fn,batch,checked_output_push);
}
#endif

void
CheckLength::add_handlers()
{
    add_data_handlers("length", Handler::OP_READ | Handler::OP_WRITE, &_max);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CheckLength)
ELEMENT_MT_SAFE(CheckLength)
