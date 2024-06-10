// -*- c-basic-offset: 4 -*-
/*
 * expunqueue.{cc,hh} -- element pulls as many packets as possible from
 * its input, pushes them out its output
 * Tom Barbette
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
#include "expunqueue.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/standard/scheduleinfo.hh>
CLICK_DECLS

EXPUnqueue::EXPUnqueue()
    : _task(this), _timer(&_task), _runs(0), _packets(0), _pushes(0), _failed_pulls(0), _empty_runs(0), _burst(32), _active(true)
{
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

int
EXPUnqueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(this, errh).bind(conf)
	    .read_or_set("BURST", _burst, 32)
        .read_or_set("ACTIVE", _active, true)
        .consume() < 0)
        return -1;
    return configure_helper(is_bandwidth(), this, conf, errh);
}

double
EXPUnqueue::ran_expo(double lambda){
    //DEPRECATED
    /*double u;
    u = (double)_gen() / (_gen.max() + 1.0);
    return -log(1- u) / lambda;*/
    return _poisson(_gen) * (double)cycles_hz();

}

int
EXPUnqueue::configure_helper(bool is_bandwidth, Element *elt, Vector<String> &conf, ErrorHandler *errh)
{
    unsigned r;
    unsigned dur_msec = 20;
    unsigned tokens;
    bool dur_specified, tokens_specified;
    const char *burst_size = is_bandwidth ? "BURST_BYTES" : "BURST_SIZE";
    int s = 0;

    Args args(conf, elt, errh);

	 if (args.read_mp("RATE", r)
         .read("SEED", s)
	.read(burst_size, tokens).read_status(tokens_specified)
	.complete() < 0)
	return -1;

    if (s == 0)
        _gen = std::mt19937(rand());
    else
        _gen = std::mt19937(s);
        
    _lambda = (double)1.0f * ((double)r);
    click_chatter("Lambda %f, cycles %lu",_lambda,cycles_hz());
    _last =0;
    _poisson = std::exponential_distribution<> (_lambda);
    _inter_arrival_time = 0;// ran_expo(_lambda);
    //click_chatter("First packet at %lu",_inter_arrival_time);
    return 0;
}

int
EXPUnqueue::initialize(ErrorHandler *errh)
{
    ScheduleInfo::initialize_task(this, &_task, errh);
    _signal = Notifier::upstream_empty_signal(this, 0, &_task);
    _timer.initialize(this);
    return 0;
}



void EXPUnqueue::refill(uint64_t now) {

    while (_last < now) {
        
        _last += _inter_arrival_time;
        _bucket++;
        _inter_arrival_time = ran_expo(_lambda);
        //click_chatter("Next in %lu",_inter_arrival_time);
    }
}

bool
EXPUnqueue::run_task(Task *)
{
    bool worked = false;
    _runs++;

    if (!_active)
	return false;
    uint64_t now = click_get_cycles();
    if (_last == 0)
        _last = now;
    refill(now);
    if (_bucket > 0) {
#if HAVE_BATCH
            int burst = _bucket;
            if (burst > (int)_burst)
                burst = _burst;
            PacketBatch* batch = input(0).pull_batch(burst);
            if (batch) {
                int c = batch->count();
                _bucket -= c;
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
                _bucket -= 1;
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
        uint64_t now = click_get_cycles();
        refill(now);
        
        _timer.schedule_after(Timestamp::make_nsec((double)(_last + _inter_arrival_time  - now) / (cycles_hz() / 1000000000)));
        _empty_runs++;
        return false;
    }
    _task.fast_reschedule();
    return worked;
}

String
EXPUnqueue::read_handler(Element *e, void *thunk)
{
    EXPUnqueue *ru = (EXPUnqueue *)e;
    switch ((uintptr_t) thunk) {
      case h_rate:
	    return String(1.0f/ru->_lambda);
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
EXPUnqueue::write_param(const String &conf, Element *e, void *user_data,
		     ErrorHandler *errh)
{
    EXPUnqueue *u = static_cast<EXPUnqueue *>(e);
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
EXPUnqueue::add_handlers()
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
EXPORT_ELEMENT(EXPUnqueue)
ELEMENT_MT_SAFE(EXPUnqueue)
