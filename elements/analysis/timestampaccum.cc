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
TimestampAccumBase<T>::initialize(ErrorHandler *)
{
    PER_THREAD_MEMBER_SET(_state, usec_accum, 0);
    PER_THREAD_MEMBER_SET(_state, count, 0);
    return 0;
}

template <template <typename> class T> void
TimestampAccumBase<T>::push(int, Packet *p)
{
    State& state = *_state;
    state.usec_accum += (Timestamp::now() - p->timestamp_anno()).usecval();
    state.count++;
    output(0).push(p);
}

#if HAVE_BATCH
template <template <typename> class T> void
TimestampAccumBase<T>::push_batch(int, PacketBatch *b)
{
    State& state = *_state;
    unsigned c = 0;
    double acc = 0;
    Timestamp now = Timestamp::now();
    FOR_EACH_PACKET(b, p) {
        acc += (now - p->timestamp_anno()).usecval();
        ++c;
    }
    state.count += c;
    state.usec_accum += acc;
    output(0).push_batch(b);
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
        total.usec_accum += state.usec_accum;
    }
    int which = reinterpret_cast<intptr_t>(thunk);
    switch (which) {
      case 0:
	return String(total.count);
      case 1:
	return String(total.usec_accum);
      case 2:
	return String(total.usec_accum / total.count);
      default:
	return String();
    }
}

template <template <typename> class T> int
TimestampAccumBase<T>::reset_handler(const String &, Element *e, void *, ErrorHandler *)
{
    TimestampAccumBase<T> *ta = static_cast<TimestampAccumBase<T> *>(e);
    PER_THREAD_MEMBER_SET(ta->_state, usec_accum, 0);
    PER_THREAD_MEMBER_SET(ta->_state, count, 0);
    return 0;
}

template <template <typename> class T> void
TimestampAccumBase<T>::add_handlers()
{
    add_read_handler("count", read_handler, 0);
    add_read_handler("time", read_handler, 1);
    add_read_handler("average_time", read_handler, 2);
    add_write_handler("reset_counts", reset_handler, 0, Handler::f_button);
}

template class TimestampAccumBase<per_thread>;
template class TimestampAccumBase<not_per_thread>;


CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel int64)
EXPORT_ELEMENT(TimestampAccum)
EXPORT_ELEMENT(TimestampAccumMP)
ELEMENT_MT_SAFE(TimestampAccumMP)
