/*
 * rtcycles.{cc,hh} -- measures round trip cycles on a push or pull path.
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
#include <click/error.hh>
#include <click/glue.hh>
#include "rtcycles.hh"

RTCycles::RTCycles() : _state()
{
}

RTCycles::~RTCycles()
{
}

void
RTCycles::push(int, Packet *p)
{
  click_cycles_t c = click_get_cycles();
  output(0).push(p);
  _state->accum += click_get_cycles() - c;
  _state->npackets++;
}

Packet *
RTCycles::pull(int)
{
  click_cycles_t c = click_get_cycles();
  Packet *p = input(0).pull();
  _state->accum += click_get_cycles() - c;
  if (p) _state->npackets++;
  return(p);
}

#if HAVE_BATCH
void
RTCycles::push_batch(int, PacketBatch *p)
{
  click_cycles_t c = click_get_cycles();
  int count = p->count();
  output(0).push_batch(p);
  _state->accum += click_get_cycles() - c;
  _state->npackets += count;
}

PacketBatch *
RTCycles::pull_batch(int,unsigned max)
{
  click_cycles_t c = click_get_cycles();
  PacketBatch *p = input(0).pull_batch(max);
  _state->accum += click_get_cycles() - c;
  if (p) _state->npackets += p->count();
      return(p);
}

#endif

String
RTCycles::read_handler(Element *e, void *thunk)
{
	RTCycles *cca = static_cast<RTCycles *>(e);
    switch ((uintptr_t)thunk) {
      case 0: {
	  PER_THREAD_MEMBER_SUM(uint64_t,count,cca->_state,npackets);
	  return String(count); }
      case 1: {
	  PER_THREAD_MEMBER_SUM(uint64_t,accum,cca->_state,accum);
	  return String(accum); }
      default:
	  return String();
    }
}

int
RTCycles::reset_handler(const String &, Element *e, void *, ErrorHandler *)
{
	RTCycles *cca = static_cast<RTCycles *>(e);
    PER_THREAD_MEMBER_SET(cca->_state,accum,0);
    PER_THREAD_MEMBER_SET(cca->_state,npackets,0);
    return 0;
}

void
RTCycles::add_handlers()
{
    add_read_handler("packets", read_handler, 0);
    add_read_handler("count", read_handler, 0);
    add_read_handler("cycles", read_handler, 1);
    add_write_handler("reset_counts", reset_handler, 0, Handler::BUTTON);
}


ELEMENT_REQUIRES(int64)
EXPORT_ELEMENT(RTCycles)
