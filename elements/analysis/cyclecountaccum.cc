// -*- c-basic-offset: 4 -*-
/*
 * cyclecountaccum.{cc,hh} -- accumulate cycle counter deltas
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
#include "cyclecountaccum.hh"
#include <click/packet_anno.hh>
#include <click/glue.hh>

CycleCountAccum::CycleCountAccum()
    : _state()
{
}

CycleCountAccum::~CycleCountAccum()
{
}

inline Packet*
CycleCountAccum::simple_action(Packet *p)
{
	state &s = *_state;
    if (PERFCTR_ANNO(p)) {
	s.accum += click_get_cycles() - PERFCTR_ANNO(p);
	s.count++;
    } else {
	s.zero_count++;
	if (s.zero_count == 1)
		click_chatter("%s: packet with zero cycle counter annotation!", declaration().c_str());
    }
    return p;
}

String
CycleCountAccum::read_handler(Element *e, void *thunk)
{
    CycleCountAccum *cca = static_cast<CycleCountAccum *>(e);
    switch ((uintptr_t)thunk) {
      case 0: {
	  PER_THREAD_MEMBER_SUM(uint64_t,count,cca->_state,count);
	  return String(count); }
      case 1: {
	  PER_THREAD_MEMBER_SUM(uint64_t,accum,cca->_state,accum);
	  return String(accum); }
      case 2: {
	  PER_THREAD_MEMBER_SUM(uint64_t,zero_count,cca->_state,zero_count);
	  return String(zero_count); }
      case 3: {
	  PER_THREAD_MEMBER_SUM(uint64_t,accum,cca->_state,accum);
	  PER_THREAD_MEMBER_SUM(uint64_t,count,cca->_state,count);
	  return String(accum / count); }
      default:
	  return String();
    }
}

int
CycleCountAccum::reset_handler(const String &, Element *e, void *, ErrorHandler *)
{
    CycleCountAccum *cca = static_cast<CycleCountAccum *>(e);
    PER_THREAD_MEMBER_SET(cca->_state,accum,0);
    PER_THREAD_MEMBER_SET(cca->_state,count,0);
    PER_THREAD_MEMBER_SET(cca->_state,zero_count,0);
    return 0;
}

void
CycleCountAccum::add_handlers()
{
    add_read_handler("count", read_handler, 0);
    add_read_handler("cycles", read_handler, 1);
    add_read_handler("zero_count", read_handler, 2);
    add_read_handler("cycles_pp", read_handler, 3);
    add_write_handler("reset_counts", reset_handler, 0, Handler::BUTTON);
}

ELEMENT_REQUIRES(int64)
EXPORT_ELEMENT(CycleCountAccum)
ELEMENT_MT_SAFE(CycleCountAccum)
