/*
 * averagecounter.{cc,hh} -- element counts packets, measures duration
 * Benjie Chen
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
#include "averagecounter.hh"
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/sync.hh>
#include <click/glue.hh>
#include <click/error.hh>
CLICK_DECLS

AverageCounter::AverageCounter()
{
    _mp = false;
    _link_fcs = true;
}

void
AverageCounter::reset()
{
  _count = 0;
  _byte_count = 0;
  _first = 0;
  _last = 0;
}

int
AverageCounter::configure(Vector<String> &conf, ErrorHandler *errh)
{
#if HAVE_FLOAT_TYPES
  double ignore = 0;
#else
  int ignore = 0;
#endif
  if (Args(conf, this, errh)
          .read_p("IGNORE", ignore)
          .read_p("LINK_FCS", _link_fcs)
          .complete() < 0)
    return -1;
#if HAVE_FLOAT_TYPES
  _ignore = (double) ignore * CLICK_HZ;
#else
  _ignore = ignore * CLICK_HZ;
#endif
  return 0;
}

int
AverageCounter::initialize(ErrorHandler *)
{
  reset();
  return 0;
}

#if HAVE_BATCH
PacketBatch *
AverageCounter::simple_action_batch(PacketBatch *batch)
{
    click_jiffies_t jpart = click_jiffies();
    if (_first == 0)
        _first.compare_swap(0, jpart);
    if (jpart - _first >= _ignore) {
        uint64_t l = 0;
        FOR_EACH_PACKET(batch,p) {
            l+=p->length();
        }
		add_count(batch->count(), l);
	}
    _last = jpart;
    return batch;
}
#endif

Packet *
AverageCounter::simple_action(Packet *p)
{
    click_jiffies_t jpart = click_jiffies();
    if (_first == 0)
	_first.compare_swap(0, jpart);
    if (jpart - _first >= _ignore) {
	    add_count(1,p->length());
    }
    _last = jpart;
    return p;
}

uint64_t get_count(AverageCounter* c, int user_data) {
  switch(user_data) {
    case 3:
      return (c->byte_count() + (c->count() * (20 + (c->_link_fcs?4:0) )) ) << 3;
    case 2:
      return c->byte_count() * 8;
    case 1:
      return c->byte_count();
    default:
      return c->count();
  }
}

static String
averagecounter_read_count_handler(Element *e, void *thunk)
{
  int user_data = (long)thunk;
  AverageCounter *c = (AverageCounter *)e;
  return String(get_count(c, user_data));
}

static String
averagecounter_read_rate_handler(Element *e, void *thunk)
{
  AverageCounter *c = (AverageCounter *)e;
  uint64_t d = c->last() - c->first();
  int user_data = (long)thunk;
  d -= c->ignore();
  if (d < 1) d = 1;
  uint64_t count = get_count(c, user_data);

#if CLICK_USERLEVEL
  if (user_data == 4) { //time
      return String((double)d / CLICK_HZ);
  }

  return String(((double) count * CLICK_HZ) / d);
#else
  if (user_data == 4) { //time
      return String(d / CLICK_HZ);
  }
  uint32_t rate;
  if (count < (uint32_t) (0xFFFFFFFFU / CLICK_HZ))
      rate = (count * CLICK_HZ) / d;
  else
      rate = (count / d) * CLICK_HZ;
  return String(rate);
#endif
}

static int
averagecounter_reset_write_handler
(const String &, Element *e, void *, ErrorHandler *)
{
  AverageCounter *c = (AverageCounter *)e;
  c->reset();
  return 0;
}

void
AverageCounter::add_handlers()
{
  add_read_handler("count", averagecounter_read_count_handler, 0);
  add_read_handler("byte_count", averagecounter_read_count_handler, 1);
  add_read_handler("bit_count", averagecounter_read_count_handler, 2);
  add_read_handler("link_count", averagecounter_read_count_handler, 3);
  add_read_handler("rate", averagecounter_read_rate_handler, 0);
  add_read_handler("byte_rate", averagecounter_read_rate_handler, 1);
  add_read_handler("bit_rate", averagecounter_read_rate_handler, 2);
  add_read_handler("link_rate", averagecounter_read_rate_handler, 3);
  add_read_handler("time", averagecounter_read_rate_handler, 4);
  add_write_handler("reset", averagecounter_reset_write_handler, 0, Handler::BUTTON);
}

AverageCounterMP::AverageCounterMP()
{
    _mp = true;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(AverageCounter)
EXPORT_ELEMENT(AverageCounterMP)
ELEMENT_MT_SAFE(AverageCounterMP)
