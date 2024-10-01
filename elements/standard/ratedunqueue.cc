// -*- c-basic-offset: 4 -*-
/*
 * ratedunqueue.{cc,hh} -- element pulls as many packets as possible from
 * its input, pushes them out its output
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2010 Meraki, Inc.
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
#include "ratedunqueue.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/standard/scheduleinfo.hh>
CLICK_DECLS

RatedUnqueue::RatedUnqueue()
    : _task(this), _timer(&_task), _runs(0), _packets(0), _pushes(0), _failed_pulls(0), _empty_runs(0), _burst(32), _active(true)
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

int
RatedUnqueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(this, errh).bind(conf)
	    .read_or_set("BURST", _burst, 32)
        .read_or_set("ACTIVE", _active, true)
        .consume() < 0)
        return -1;
    return configure_helper(&_tb, is_bandwidth(), this, conf, errh);
}

int
RatedUnqueue::configure_helper(TokenBucket *tb, bool is_bandwidth, Element *elt, Vector<String> &conf, ErrorHandler *errh)
{
    unsigned r;
    unsigned dur_msec = 20;
    unsigned tokens;
    bool dur_specified, tokens_specified;
    const char *burst_size = is_bandwidth ? "BURST_BYTES" : "BURST_SIZE";

    Args args(conf, elt, errh);
    if (is_bandwidth)
	args.read_mp("RATE", BandwidthArg(), r);
    else
	args.read_mp("RATE", r);
    if (args.read("BURST_DURATION", SecondsArg(3), dur_msec).read_status(dur_specified)
	.read(burst_size, tokens).read_status(tokens_specified)
	.complete() < 0)
	return -1;

    if (dur_specified && tokens_specified)
	return errh->error("cannot specify both BURST_DURATION and BURST_SIZE");
    else if (!tokens_specified) {
	bigint::limb_type res[2];
	bigint::multiply(res[1], res[0], r, dur_msec);
	bigint::divide(res, res, 2, 1000);
	tokens = res[1] ? UINT_MAX : res[0];
    }

    if (is_bandwidth) {
	unsigned new_tokens = tokens + tb_bandwidth_thresh;
	tokens = (tokens < new_tokens ? new_tokens : UINT_MAX);
    }

    tb->assign(r, tokens ? tokens : 1);
    return 0;
}

int
RatedUnqueue::initialize(ErrorHandler *errh)
{
    ScheduleInfo::initialize_task(this, &_task, errh);
    _signal = Notifier::upstream_empty_signal(this, 0, &_task);
    _timer.initialize(this);
    return 0;
}

bool
RatedUnqueue::run_task(Task *)
{
    bool worked = false;
    _runs++;

    if (!_active)
	return false;

    _tb.refill();
    if (_tb.contains(1)) {
#if HAVE_BATCH
            int burst = _tb.size();
            if (burst > (int)_burst)
                burst = _burst;
            PacketBatch* batch = input(0).pull_batch(burst);
            if (batch) {
                int c = batch->count();
                _tb.remove(c);
                _packets += c;
                _pushes++;
                worked = true;
                output(0).push_batch(batch);
            } else {
                _failed_pulls++;
                if (!_signal)
                    return false; // without rescheduling
            }
#else
            if (Packet *p = input(0).pull()) {
                _tb.remove(1);
                _packets++;
                _pushes++;
                worked = true;
                output(0).push(p);
            } else { // no Packet available
                _failed_pulls++;
                if (!_signal)
                    return false; // without rescheduling
            }
#endif
    } else {
	_timer.schedule_after(Timestamp::make_jiffies(_tb.time_until_contains(1)));
	_empty_runs++;
	return false;
    }
    _task.fast_reschedule();
    return worked;
}

String
RatedUnqueue::read_handler(Element *e, void *thunk)
{
    RatedUnqueue *ru = (RatedUnqueue *)e;
    switch ((uintptr_t) thunk) {
      case h_rate:
	if (ru->is_bandwidth())
	    return BandwidthArg::unparse(ru->_tb.rate());
	else
	    return String(ru->_tb.rate());
      case h_calls: {
	  StringAccum sa;
	  sa << ru->_runs << " calls to run_task()\n"
	     << ru->_empty_runs << " empty runs\n"
	     << ru->_pushes << " pushes\n"
	     << ru->_failed_pulls << " failed pulls\n"
	     << ru->_packets << " packets\n";
	  return sa.take_string();
      }
    }
    return String();
}

enum {h_active};
int
RatedUnqueue::write_param(const String &conf, Element *e, void *user_data,
		     ErrorHandler *errh)
{
    RatedUnqueue *u = static_cast<RatedUnqueue *>(e);
    switch (reinterpret_cast<intptr_t>(user_data)) {
    case h_active:
	    click_chatter("Active handler");
	if (!BoolArg().parse(conf, u->_active))
	    return errh->error("syntax error");
    if (u->_active && !u->_task.scheduled()) {

	    click_chatter("Scheduling");
	u->_task.reschedule();
    }

	break;

    }
    return 0;
}

void
RatedUnqueue::add_handlers()
{
    add_read_handler("calls", read_handler, h_calls);
    add_read_handler("rate", read_handler, h_rate);
    add_write_handler("rate", reconfigure_keyword_handler, "0 RATE");
    add_data_handlers("active", Handler::OP_READ | Handler::CHECKBOX, &_active);
    add_write_handler("active", write_param, h_active);
    add_task_handlers(&_task);
    add_read_handler("config", read_handler, h_rate);
    set_handler_flags("config", 0, Handler::CALM);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(RatedUnqueue)
ELEMENT_MT_SAFE(RatedUnqueue)
