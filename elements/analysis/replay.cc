// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * replay.{cc,hh} -- Replay some packets
 * Tom Barbette
 *
 * Copyright (c) 2015 University of Liege
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
#include "replay.hh"
#include <click/args.hh>
#include <click/standard/scheduleinfo.hh>
CLICK_DECLS

ReplayBase::ReplayBase() : _active(true), _loaded(false), _burst(64), _stop(-1), _quick_clone(false), _task(this), _limit(-1), _queue_head(0), _queue_current(0), _use_signal(false),_verbose(false),_freeonterminate(true), _lastsent_p(), _lastsent_real() {
#if HAVE_BATCH
    in_batch_mode = BATCH_MODE_YES;
#endif
}

ReplayBase::~ReplayBase()
{
}

/**
 * Common parsing for all replay
 */
int ReplayBase::parse(Args* args) {
    if (args->read("STOP", _stop)
             .read("QUICK_CLONE", _quick_clone)
             .read("BURST", _burst)
             .read("VERBOSE", _verbose)
             .read("FREEONTERMINATE", _freeonterminate)
             .read("LIMIT", _limit)
             .read("ACTIVE",_active)
             .execute() < 0) {
        return -1;
    }

    return 0;
}


void ReplayBase::reset_time() {
    if (_queue_current) {
        _lastsent_p = _queue_current->timestamp_anno();
        _lastsent_real = Timestamp::now_steady();
    }
}

void ReplayBase::cleanup_packets() {
    while (_queue_head) {
        Packet* next = _queue_head->next();
        _queue_head->kill();
        _queue_head = next;
    }
}

void ReplayBase::cleanup(CleanupStage) {
    cleanup_packets();
}

void
ReplayBase::set_active(bool active) {
    _active = active;
    reset_time();
    if (active)
        _task.reschedule();
    else
        _task.unschedule();
}

int
ReplayBase::write_handler(const String & s_in, Element *e, void *thunk, ErrorHandler *errh)
{
    ReplayBase *q = static_cast<ReplayBase *>(e);
    int which = reinterpret_cast<intptr_t>(thunk);
    String s = cp_uncomment(s_in);
    switch (which) {
      case 0: {
        bool active;
        if (BoolArg().parse(s, active)) {
          q->set_active(active);
          return 0;
        } else
          return errh->error("type mismatch");
        }
        return 0;
      case 1:
          q->cleanup_packets();
          q->_loaded = false;
          return 0;
      default:
        return errh->error("internal error");
    }
}

void
ReplayBase::add_handlers()
{
    add_write_handler("active", write_handler, 0, Handler::BUTTON);
    add_write_handler("reset", write_handler, 1, Handler::BUTTON);
    add_data_handlers("loaded", Handler::OP_READ, &_loaded);
    add_data_handlers("active", Handler::OP_READ, &_active);
    add_data_handlers("stop", Handler::OP_READ | Handler::OP_WRITE, &_stop);
}

Replay::Replay() : _queue(1024)
{
}

Replay::~Replay()
{
}


void *
Replay::cast(const char *n)
{
    if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0)
    return static_cast<Notifier *>(&_notifier);
    else
    return Element::cast(n);
}


int
Replay::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (ReplayBase::parse(&args) != 0)
        return -1;

    if (args
        .read("QUEUE", _queue)
        .read("USE_SIGNAL",_use_signal)
    .complete() < 0)
        return -1;

    return 0;
}

Packet* Replay::pull(int) {
    _task.reschedule();
    return _output.ring.extract();
}

#if HAVE_BATCH
PacketBatch* Replay::pull_batch(int, unsigned max) {
    PacketBatch* head;
    _task.reschedule();
    MAKE_BATCH(_output.ring.extract(),head,max);
    return head;
}
#endif

int
Replay::initialize(ErrorHandler * errh) {
    _notifier.initialize(Notifier::EMPTY_NOTIFIER, router());
    _notifier.set_active(false,false);
    _input.resize(1);
     _input[0].signal = Notifier::upstream_empty_signal(this, 0, (Task*)NULL);
    _output.ring.initialize(_queue);
    ScheduleInfo::initialize_task(this,&_task,_active,errh);
    return 0;
}



bool
Replay::run_task(Task* task)
{
    if (!_active)
        return false;

    if (unlikely(!_loaded && !load_packets()))
        return false;

    if (_stop == 0) {
        router()->please_stop_driver();
        _active = false;
        return false;
    }

    unsigned int n = 0;
    while (_queue_current != 0 && n < _burst) {
        Packet* p = _queue_current;

        if (_output.ring.is_full()) {
            _notifier.sleep();
            return n > 0;
        } else {
            _queue_current = p->next();
            Packet* q;
            if (_stop != 1 || _freeonterminate) {
                q = p->clone(_quick_clone);
            } else {
                q = p;
                _queue_head = _queue_current;
            }
            assert(_output.ring.insert(q));
            _notifier.wake();
        }
        n++;
    }

    check_end_loop(task);

    return n > 0;
}


ReplayUnqueue::ReplayUnqueue() : _timing(0)
{

}

ReplayUnqueue::~ReplayUnqueue()
{
}

int
ReplayUnqueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (ReplayBase::parse(&args) != 0)
        return -1;
    if (args
        .read("USE_SIGNAL",_use_signal)
        .read("TIMING", _timing)
        .complete() < 0)
        return -1;
    return 0;
}

int
ReplayUnqueue::initialize(ErrorHandler * errh) {
    _input.resize(1);
    _input[0].signal = Notifier::upstream_empty_signal(this, 0, (Task*)NULL);
    ScheduleInfo::initialize_task(this,&_task,true,errh);
    return 0;
}

bool
ReplayUnqueue::run_task(Task* task)
{
    if (!_active)
        return false;

    if (unlikely(!_loaded && !load_packets()))
        return false;

    if (_stop == 0) {
        router()->please_stop_driver();
        _active = false;
        return false;
    }

    Timestamp now;
    if (_timing > 0)
        now = Timestamp::now_steady();
    unsigned int n = 0;
#if HAVE_BATCH
    unsigned int c = 0;
    PacketBatch* head = 0;
    Packet* last = 0;
#endif
    while (_queue_current != 0 && n < _burst) {
            Packet* p = _queue_current;

            //If timing is activated, wait for the amount of time or resched
            if (_timing > 0) {
                const unsigned min_timing = 1; //Amount of us between packets to ignore and sent right away
                const unsigned min_sched = 10; //Amount of us that leads to rescheduling

                Timestamp tdiff = p->timestamp_anno() - _lastsent_p;
                long diff = tdiff.usecval();
                long rdiff;
                while (diff - (rdiff = ((long)(now - _lastsent_real).usecval() * _timing)) > min_timing) {
#if HAVE_BATCH
                    if (head) {
                        output_push_batch(0,head->make_tail(last,c));
                        head = 0;
                        c = 0;
                    }
#endif
                    if (diff - rdiff > min_sched) {
                        goto end;
                    }
                    now = Timestamp::now_steady();
                    click_relax_fence();
                }
            }

            _queue_current = p->next();
            Packet* q;
            if (_stop != 1 || _freeonterminate) {
                q = p->clone(_quick_clone);
            } else {
                q = p;
                _queue_head = _queue_current;
            }

#if HAVE_BATCH
            if (head == 0) {
                head = PacketBatch::start_head(q);
                last = head;
                c = 1;
            } else {
                //Just add the packet to the end of the batch
                last->set_next(q);
                last = q;
                c++;
            }
#else
            output(0).push(q);
#endif
        n++;
    }

#if HAVE_BATCH
    //Flush pending batch
    if (head)
        output_push_batch(0,head->make_tail(last,c));
#endif

end:
    check_end_loop(task);

    return n > 0;
}

void
ReplayUnqueue::add_handlers() {
    ReplayBase::add_handlers();
    add_data_handlers("timing", Handler::OP_READ | Handler::OP_WRITE, &_timing);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(Replay)
ELEMENT_MT_SAFE(Replay)
EXPORT_ELEMENT(ReplayUnqueue)
ELEMENT_MT_SAFE(ReplayUnqueue)
