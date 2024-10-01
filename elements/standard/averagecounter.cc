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

template <typename Stats>
AverageCounterBase<Stats>::AverageCounterBase()
{
    _link_fcs = true;
}


template <typename Stats>
int
AverageCounterBase<Stats>::configure(Vector<String> &conf, ErrorHandler *errh)
{
  uint32_t max = 0;
  uint32_t threshold = 0;
#if HAVE_FLOAT_TYPES
  double ignore = 0;
#else
  int ignore = 0;
#endif
  if (Args(conf, this, errh)
          .read_p("IGNORE", ignore)
          .read_p("LINK_FCS", _link_fcs)
          .read_or_set("MAX", max, 0)
          .read_or_set("THRESHOLD", threshold, 0)
          .complete() < 0)
    return -1;
#if HAVE_FLOAT_TYPES
  _ignore = (double) ignore * CLICK_HZ;
#else
  _ignore = ignore * CLICK_HZ;
#endif

  if (max > 0)
      _max = max * CLICK_HZ;
  else
      _max = UINT32_MAX;

  _threshold = threshold;
  return 0;
}

template <typename Stats>
int
AverageCounterBase<Stats>::initialize(ErrorHandler *)
{
  reset();
  return 0;
}

#if HAVE_BATCH
template <typename Stats>
PacketBatch *
AverageCounterBase<Stats>::simple_action_batch(PacketBatch *batch)
{
    click_jiffies_t jpart = click_jiffies();
    if (_stats.my_first() == 0)
        _stats.set_first(jpart);
    auto d = jpart - _stats.my_first();
    if (likely(d >= _ignore && d < _max)) {
        uint64_t l = 0;
        FOR_EACH_PACKET(batch,p) {
            l+=p->length();
        }
        _stats.add_count(batch->count(), l);
        _stats.set_last(jpart);
        if (unlikely(_threshold > 0 && jpart - _stats.my_first() > CLICK_HZ)) {
            uint64_t rate = _stats.count() * 1000 / (jpart - _stats.first());
            if (rate > _threshold) {
                click_chatter("%p{element} : starting compute (rate %lu pps)", this, rate);
                _threshold = 0;
                _stats.reset();
            }
        }

    } else {
        if (d < _max)
            _stats.set_last(jpart);
    }

    return batch;
}
#endif

template <typename Stats>
Packet *
AverageCounterBase<Stats>::simple_action(Packet *p)
{
    click_jiffies_t jpart = click_jiffies();
    if (_stats.my_first() == 0)
    _stats.set_first(jpart);
    if (jpart - _stats.my_first() >= _ignore) {
        _stats.add_count(1,p->length());
    }
    _stats.set_last(jpart);
    return p;
}

template <typename Stats>
uint64_t get_count(AverageCounterBase<Stats>* c, int user_data) {
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

template <typename Stats>
static String
averagecounter_read_count_handler(Element *e, void *thunk)
{
  int user_data = (long)thunk;
  AverageCounterBase<Stats> *c = (AverageCounterBase<Stats> *)e;
  return String(get_count(c, user_data));
}

template <typename Stats>
String
AverageCounterBase<Stats>::averagecounter_read_rate_handler(Element *e, void *thunk)
{
  AverageCounterBase<Stats> *c = (AverageCounterBase<Stats> *)e;
  uint64_t d = c->_stats.last() - c->_stats.first();
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

template <typename Stats>
static int
averagecounter_reset_write_handler
(const String &, Element *e, void *, ErrorHandler *)
{
  AverageCounterBase<Stats> *c = (AverageCounterBase<Stats> *)e;
  c->reset();
  return 0;
}

template <typename Stats>
void
AverageCounterBase<Stats>::add_handlers()
{
  add_read_handler("count", averagecounter_read_count_handler<Stats>, 0);
  add_read_handler("byte_count", averagecounter_read_count_handler<Stats>, 1);
  add_read_handler("bit_count", averagecounter_read_count_handler<Stats>, 2);
  add_read_handler("link_count", averagecounter_read_count_handler<Stats>, 3);
  add_read_handler("rate", averagecounter_read_rate_handler, 0);
  add_read_handler("byte_rate", averagecounter_read_rate_handler, 1);
  add_read_handler("bit_rate", averagecounter_read_rate_handler, 2);
  add_read_handler("link_rate", averagecounter_read_rate_handler, 3);
  add_read_handler("time", averagecounter_read_rate_handler, 4);
  add_write_handler("reset", averagecounter_reset_write_handler<Stats>, 0, Handler::BUTTON);
  add_write_handler("reset_now", averagecounter_reset_write_handler<Stats>, 1, Handler::BUTTON);
}

AverageCounter::AverageCounter()
{
}

AverageCounterMP::AverageCounterMP()
{
}

AverageCounterIMP::AverageCounterIMP()
{
}

template class AverageCounterBase<AverageCounterStats<uint64_t> >;
template class AverageCounterBase<AverageCounterStats<atomic_uint64_t> >;
template class AverageCounterBase<AverageCounterStatsIMP>;

CLICK_ENDDECLS
EXPORT_ELEMENT(AverageCounter)
EXPORT_ELEMENT(AverageCounterMP)
EXPORT_ELEMENT(AverageCounterIMP)
ELEMENT_MT_SAFE(AverageCounterMP)
ELEMENT_MT_SAFE(AverageCounterIMP)
