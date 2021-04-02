/*
 * rrswitch.{cc,hh} -- round robin switch element
 * Eddie Kohler
 *
 * Copyright (c) 2000 Mazu Networks, Inc.
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
#include <click/args.hh>
#include "rrswitch.hh"
CLICK_DECLS

RoundRobinSwitch::RoundRobinSwitch()
{
  _next = 0;
  _max = 0;
}

int
RoundRobinSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int max = -1;
    bool split_batch = true;
    if (Args(conf, this, errh)
        .read_p("MAX", max)
        .read("SPLITBATCH",split_batch)
        .complete() < 0)
	    return -1;

    if (max < 0)
        max = noutputs();
    _max = max;
    _split_batch = split_batch;
    return 0;
}

inline int
RoundRobinSwitch::next(Packet*)
{
    int i = _next;
  #ifndef HAVE_MULTITHREAD
    _next++;
    if (_next >= _max)
      _next = 0;
  #else
    click_read_fence();
    // in MT case try our best to be rr, but don't worry about it if we mess up
    // once in awhile
    uint32_t newval = i+1;
    if (newval >= _max)
      newval = 0;
  # if ! CLICK_ATOMIC_COMPARE_SWAP
    _next.compare_and_swap(i, newval);
  # else
    _next.compare_swap(i, newval);
  # endif
  #endif
    return i;
}

void
RoundRobinSwitch::push(int, Packet *p)
{
    output(next(p)).push(p);
}

#if HAVE_BATCH
void
RoundRobinSwitch::push_batch(int, PacketBatch *batch)
{
    if (_split_batch) {
      CLASSIFY_EACH_PACKET(_max,next,batch,output_push_batch);
    } else {
      output(next(batch->first())).push_batch(batch);
    }
}
#endif

void RoundRobinSwitch::add_handlers() {
    add_data_handlers("max", Handler::f_read | Handler::f_write, &_max);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RoundRobinSwitch)
ELEMENT_MT_SAFE(RoundRobinSwitch)
