// -*- c-basic-offset: 4 -*-
/*
 * timestampaccum.{cc,hh} -- accumulate cycle counter deltas
 * Eddie Kohler
 * Tom Barbette
 *
 * Copyright (c) 2002 International Computer Science Institute
 * Copyright (c) 2018 KTH Institute of Technology
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
#include "timestampaccum.hh"
#include <click/glue.hh>
#include <click/sync.hh>
CLICK_DECLS

template <template <typename> class T>
TimestampAccumBase<T>::TimestampAccumBase()
{
}

template <template <typename> class T>
TimestampAccumBase<T>::~TimestampAccumBase()
{
}

template <template <typename> class T> int
TimestampAccumBase<T>::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _steady = true;
    if (Args(conf, this, errh)
	.read_or_set("STEADY", _steady, true)
	.complete() < 0)
	return -1;
    return 0;
}

template <template <typename> class T> int
TimestampAccumBase<T>::initialize(ErrorHandler *)
{
    PER_THREAD_MEMBER_SET(_state, nsec_accum, 0);
    PER_THREAD_MEMBER_SET(_state, count, 0);
    return 0;
}

template <template <typename> class T> void
TimestampAccumBase<T>::push(int port, Packet *p)
{
    State& state = *_state;
    Timestamp src;
    if (_steady)
        src = Timestamp::now_steady();
    else
        src = Timestamp::now();
    uint64_t val = (src - p->timestamp_anno()).nsecval();
    if (val > state.nsec_max)
        state.nsec_max = val;
    if (val < state.nsec_min)
        state.nsec_min = val;
    state.nsec_accum += val;
    state.count++;
    output(port).push(p);
}

#if HAVE_BATCH
template <template <typename> class T> void
TimestampAccumBase<T>::push_batch(int port, PacketBatch *b)
{
    State& state = *_state;
    unsigned c = 0;
    double acc = 0;
    Timestamp now = Timestamp::now();
    FOR_EACH_PACKET(b, p) {
        uint64_t val = (now - p->timestamp_anno()).nsecval();
        acc += val;
        ++c;
        if (val > state.nsec_max)
            state.nsec_max = val;
        if (val < state.nsec_min)
            state.nsec_min = val;
    }
    state.count += c;
    state.nsec_accum += acc;
    output(port).push_batch(b);
}
#endif

template <template <typename> class T> String
TimestampAccumBase<T>::read_handler(Element *e, void *thunk)
{
    State total;
    TimestampAccumBase<T> *ta = static_cast<TimestampAccumBase<T> *>(e);
    for (int i = 0; i < ta->_state.weight(); i++) {
        State& state = ta->_state.get_value(i);
        total.count += state.count;
        total.nsec_accum += state.nsec_accum;
        if (state.nsec_min < total.nsec_min)
            total.nsec_min = state.nsec_min;
        if (state.nsec_max > total.nsec_max)
            total.nsec_max = state.nsec_max;
    }
    int which = reinterpret_cast<intptr_t>(thunk);
    switch (which) {
      case 0:
          return String(total.count);
      case 1:
          return String(total.nsec_accum);
      case 2:
          return String((double)total.nsec_accum / total.count);
      case 3:
          if (total.nsec_min == UINT64_MAX)
              return "0";
        return String(total.nsec_min);
      case 4:
        return String(total.nsec_max);
      default:
	return String();
    }
}

template <template <typename> class T> int
TimestampAccumBase<T>::reset_handler(const String &, Element *e, void *, ErrorHandler *)
{
    TimestampAccumBase<T> *ta = static_cast<TimestampAccumBase<T> *>(e);
    PER_THREAD_MEMBER_SET(ta->_state, nsec_accum, 0);
    PER_THREAD_MEMBER_SET(ta->_state, count, 0);
    return 0;
}

template <template <typename> class T> void
TimestampAccumBase<T>::add_handlers()
{
    add_read_handler("count", read_handler, 0);
    add_read_handler("time", read_handler, 1);
    add_read_handler("average_time", read_handler, 2);
    add_read_handler("min", read_handler, 3);
    add_read_handler("max", read_handler, 4);
    add_write_handler("reset_counts", reset_handler, 0, Handler::f_button);
}

template class TimestampAccumBase<per_thread>;
template class TimestampAccumBase<not_per_thread>;

String
TimestampAccumMP::read_handler(Element *e, void *thunk)
{
    int which = reinterpret_cast<intptr_t>(thunk);
    if (which < 5)
        return TimestampAccumBase<per_thread>::read_handler(e, thunk);

    StringAccum accum;
    TimestampAccumMP *ta = static_cast<TimestampAccumMP *>(e);

    for (int i = 0; i < ta->_state.weight(); i++) {
        State &s = ta->_state.get_value(i);
        if (i > 0)
            accum << " ";
        switch (which) {
          case 5:
              accum << String(s.count); break;
          case 6:
              accum << String(s.nsec_accum); break;
          case 7:
              accum << String((double)s.nsec_accum / s.count); break;
          case 8:
              if (s.nsec_min == UINT64_MAX)
                  accum << "0";
              else
                accum << String(s.nsec_min);
              break;
          case 9:
              accum << String(s.nsec_max); break;
        }
    }
    return accum.take_string();
}

void
TimestampAccumMP::add_handlers()
{
    TimestampAccumBase<per_thread>::add_handlers();
    add_read_handler("mp_count", read_handler, 5);
    add_read_handler("mp_time", read_handler, 6);
    add_read_handler("mp_average_time", read_handler, 7);
    add_read_handler("mp_min", read_handler, 8);
    add_read_handler("mp_max", read_handler, 9);
}




CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel int64)
EXPORT_ELEMENT(TimestampAccum)
EXPORT_ELEMENT(TimestampAccumMP)
ELEMENT_MT_SAFE(TimestampAccumMP)
