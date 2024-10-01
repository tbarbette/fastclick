/*
 * ratedsource.{cc,hh} -- generates configurable rated stream of packets.
 * Benjie Chen, Eddie Kohler (based on udpgen.o)
 *
 * Computational batching support by Georgios Katsikas
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2008 Regents of the University of California
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
#include "ratedsource.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/straccum.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/glue.hh>
#include <click/handlercall.hh>
CLICK_DECLS

const unsigned RatedSource::NO_LIMIT;
const unsigned RatedSource::DEF_BATCH_SIZE;

RatedSource::RatedSource()
  : _packet(0), _task(this), _timer(&_task), _end_h(0)
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
        _batch_size   = DEF_BATCH_SIZE;
    #endif
}

int
RatedSource::configure(Vector<String> &conf, ErrorHandler *errh)
{
    ActiveNotifier::initialize(Notifier::EMPTY_NOTIFIER, router());

    String data =
      "Random bullshit in a packet, at least 64 bytes long. Well, now it is.";
    unsigned rate = 10;
    unsigned bandwidth = 0;
    int limit = -1;
    int datasize = -1;
    bool active = true, stop = false;
    _headroom = Packet::default_headroom;
    HandlerCall end_h;

    if (Args(conf, this, errh)
        .read_p("DATA", data)
        .read_p("RATE", rate)
        .read_p("LIMIT", limit)
        .read_p("ACTIVE", active)
	  .read("HEADROOM", _headroom)
        .read("LENGTH", datasize)
        .read("DATASIZE", datasize) // deprecated
        .read("STOP", stop)
        .read("BANDWIDTH", BandwidthArg(), bandwidth)
        .read("END_CALL", HandlerCallArg(HandlerCall::writable), end_h)
        .complete() < 0)
        return -1;

    _data = data;
    _datasize = datasize;

    if (bandwidth > 0) {
        rate = bandwidth / (_datasize < 0 ? _data.length() : _datasize);
    }

    int burst = rate < 200 ? 2 : rate / 100;
    if (bandwidth > 0 && burst < 2 * datasize) {
        burst = 2 * datasize;
    }

#if HAVE_BATCH
    if ( burst < (int)_batch_size ) {
        _batch_size = burst;
    }
#endif

    _tb.assign(rate, burst);
    _limit = (limit >= 0 ? unsigned(limit) : NO_LIMIT);
    _active = active;
    _stop = stop;

    delete _end_h;
    if (end_h)
        _end_h = new HandlerCall(end_h);
    else if (stop)
        _end_h = new HandlerCall("stop");
    else
        _end_h = 0;

    return 0;
}

int
RatedSource::initialize(ErrorHandler *errh)
{
    setup_packet();

    _count = 0;
    if (output_is_push(0)) {
        ScheduleInfo::initialize_task(this, &_task, errh);
        _nonfull_signal = Notifier::downstream_full_signal(this, 0, &_task);
    }

#if HAVE_BATCH
    _tb.set(_batch_size);
#else
    _tb.set(1);
#endif

    _timer.initialize(this);

    if (_end_h && _end_h->initialize_write(this, errh) < 0)
        return -1;

    return 0;
}

void
RatedSource::cleanup(CleanupStage)
{
    if (_packet)
        _packet->kill();

    _packet = 0;
    delete _end_h;
}

bool
RatedSource::run_task(Task *)
{
    if (!_active)
        return false;
    if (_limit != NO_LIMIT && _count >= _limit) {
        if (_stop)
            router()->please_stop_driver();
        return false;
    }

    // Refill the token bucket
    _tb.refill();

#if HAVE_BATCH
    PacketBatch *head = 0;
    Packet      *last;

    unsigned n = _batch_size;
    unsigned count = 0;

    if (_limit != NO_LIMIT && n + _count >= _limit)
        n = _limit - _count;

    // Create a batch
    for (int i=0 ; i<(int)n; i++) {
        if (_tb.remove_if(1)) {
            Packet *p = _packet->clone();
            p->set_timestamp_anno(Timestamp::now());

            if (head == NULL) {
                head = PacketBatch::start_head(p);
            } else {
                last->set_next(p);
            }
            last = p;

            count++;
        } else {
            _timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(_batch_size)));
            return false;
        }
    }

    // Push the batch
    if (head) {
        output_push_batch(0, head->make_tail(last, count));
        _count += count;

        _task.fast_reschedule();
        return true;
    } else {
        if (_end_h && _limit >= 0 && _count >= (ucounter_t) _limit)
            (void) _end_h->call_write();
        _timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(1)));

        return false;
    }
#else
    if (_tb.remove_if(1)) {
        Packet *p = _packet->clone();
        p->set_timestamp_anno(Timestamp::now());
        output(0).push(p);
        _count++;
        _task.fast_reschedule();
        return true;
    } else {
        if (_end_h && _limit >= 0 && _count >= (ucounter_t) _limit)
            (void) _end_h->call_write();
        _timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(1)));

        return false;
    }
#endif
}

Packet *
RatedSource::pull(int)
{
    if (!_active)
    return 0;
    if (_limit != NO_LIMIT && _count >= _limit) {
    if (_stop)
        router()->please_stop_driver();
    return 0;
    }

    _tb.refill();

    if (_tb.remove_if(1)) {
        _count++;
        Packet *p = _packet->clone();
        p->set_timestamp_anno(Timestamp::now());
        return p;
    }

    return 0;
}

#if HAVE_BATCH
PacketBatch *
RatedSource::pull_batch(int port, unsigned max) {
    PacketBatch *batch;
    MAKE_BATCH(RatedSource::pull(port), batch, max);
    return batch;
}
#endif

void
RatedSource::setup_packet()
{
    if (_packet)
        _packet->kill();

    if (_datasize < 0) {
        _packet = Packet::make(_headroom, _data.data(), _data.length(), 0);
    } else if (_datasize <= _data.length()) {
        _packet = Packet::make(_headroom, _data.data(), _datasize, 0);
    } else {
        // make up some data to fill extra space
        StringAccum sa;
        while (sa.length() < _datasize)
            sa << _data;
        _packet = Packet::make(_headroom, sa.data(), _datasize, 0);
    }
}

String
RatedSource::read_param(Element *e, void *vparam)
{
  RatedSource *rs = (RatedSource *)e;
  switch ((intptr_t)vparam) {
   case 0:            // data
    return rs->_data;
   case 1:            // rate
    return String(rs->_tb.rate());
   case 2:            // limit
    return (rs->_limit != NO_LIMIT ? String(rs->_limit) : String("-1"));
   default:
    return "";
  }
}

int
RatedSource::change_param(const String &s, Element *e, void *vparam, ErrorHandler *errh)
{
    RatedSource *rs = (RatedSource *)e;
    switch ((intptr_t)vparam) {
      case 0:            // data
          rs->_data = s;
          if (rs->_packet)
              rs->_packet->kill();
          rs->_packet = Packet::make(rs->_data.data(), rs->_data.length());
          break;
      case 1: {            // rate
          unsigned rate;
          if (!IntArg().parse(s, rate))
              return errh->error("syntax error");
          rs->_tb.assign_adjust(rate, rate < 200 ? 2 : rate / 100);
          break;
      }
      case 2: {            // limit
          int limit;
          if (!IntArg().parse(s, limit))
             return errh->error("syntax error");
          rs->_limit = (limit >= 0 ? unsigned(limit) : NO_LIMIT);
          break;
      }
      case 3: {            // active
          bool active;
          if (!BoolArg().parse(s, active))
              return errh->error("syntax error");
          rs->_active = active;
          if (rs->output_is_push(0) && !rs->_task.scheduled() && active) {
#if HAVE_BATCH
              rs->_tb.set(DEF_BATCH_SIZE);
#else
              rs->_tb.set(1);
#endif
              rs->_task.reschedule();
          }
          break;
      }
      case 5: {            // reset
          rs->_count = 0;
#if HAVE_BATCH
          rs->_tb.set(DEF_BATCH_SIZE);
#else
          rs->_tb.set(1);
#endif
          if (rs->output_is_push(0) && !rs->_task.scheduled() && rs->_active)
          rs->_task.reschedule();
          break;
      }
      case 6: {            // datasize
          int datasize;
          if (!IntArg().parse(s, datasize))
          return errh->error("syntax error");
          rs->_datasize = datasize;
          rs->setup_packet();
          break;
      }
  }

  return 0;
}

void
RatedSource::add_handlers()
{
    add_read_handler("data", read_param, 0, Handler::f_calm);
    add_write_handler("data", change_param, 0, Handler::f_raw);
    add_read_handler("rate", read_param, 1);
    add_write_handler("rate", change_param, 1);
    add_read_handler("limit", read_param, 2, Handler::f_calm);
    add_write_handler("limit", change_param, 2);
    add_data_handlers("active", Handler::f_read | Handler::f_checkbox, &_active);
    add_write_handler("active", change_param, 3);
    add_data_handlers("count", Handler::f_read, &_count);
    add_write_handler("reset", change_param, 5, Handler::f_button);
    add_data_handlers("length", Handler::f_read, &_datasize);
    add_write_handler("length", change_param, 6);
    //deprecated
    add_data_handlers("datasize", Handler::f_read | Handler::f_deprecated, &_datasize);
    add_write_handler("datasize", change_param, 6);

    if (output_is_push(0))
        add_task_handlers(&_task);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RatedSource)
