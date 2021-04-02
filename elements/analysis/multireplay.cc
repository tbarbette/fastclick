// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * multireplay.{cc,hh} -- MultiReplay some packets
 * Tom Barbette
 *
 * Copyright (c) 2015-2017 University of Liege
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
#include "multireplay.hh"
#include <click/args.hh>
#include <click/standard/scheduleinfo.hh>
CLICK_DECLS

MultiReplay::MultiReplay() : _queue(1024)
{
}

MultiReplay::~MultiReplay()
{
}


void *
MultiReplay::cast(const char *n)
{
    if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0)
    return static_cast<Notifier *>(&_notifier);
    else
    return Element::cast(n);
}


int
MultiReplay::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (ReplayBase::parse(&args) != 0)
        return -1;

    if (args
        .read_p("QUEUE", _queue)
        .read("USE_SIGNAL",_use_signal)
    .complete() < 0)
    return -1;
    return 0;
}

Packet* MultiReplay::pull(int port) {
    _task.reschedule();
    return _output[port].ring.extract();
}

#if HAVE_BATCH
PacketBatch* MultiReplay::pull_batch(int port, unsigned max) {
    PacketBatch* head;
    _task.reschedule();
    MAKE_BATCH(_output[port].ring.extract(),head,max);
    return head;
}
#endif

int
MultiReplay::initialize(ErrorHandler * errh) {
    _notifier.initialize(Notifier::EMPTY_NOTIFIER, router());
    _notifier.set_active(false,false);
    _input.resize(ninputs());
    for (int i = 0 ; i < ninputs(); i++) {
        _input[i].signal = Notifier::upstream_empty_signal(this, i, (Task*)NULL);
    }
    _output.resize(noutputs());
    for (int i = 0; i < _output.size(); i++) {
        _output[i].ring.initialize(_queue);
    }
    ScheduleInfo::initialize_task(this,&_task,_active,errh);
    return 0;
}



bool
MultiReplay::run_task(Task* task)
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

        if (_output[PAINT_ANNO(p)].ring.is_full()) {
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
            assert(_output[PAINT_ANNO(p)].ring.insert(q));
            _notifier.wake();
        }
        n++;
    }

    check_end_loop(task, n == 0);

    return n > 0;
}


MultiReplayUnqueue::MultiReplayUnqueue()
{

}

MultiReplayUnqueue::~MultiReplayUnqueue()
{
}


int
MultiReplayUnqueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    Args args(conf, this, errh);
    if (ReplayBase::parse(&args) != 0)
        return -1;

    if (args
        .read("QUICK_CLONE", _quick_clone)
        .read("USE_SIGNAL",_use_signal)
        .complete() < 0)
    return -1;
    return 0;
}

int
MultiReplayUnqueue::initialize(ErrorHandler * errh) {
    _input.resize(ninputs());
    for (int i = 0 ; i < ninputs(); i++) {
        _input[i].signal = Notifier::upstream_empty_signal(this, i, (Task*)NULL);
    }
    ScheduleInfo::initialize_task(this,&_task,true,errh);
    return 0;
}

bool
MultiReplayUnqueue::run_task(Task* task)
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
#if HAVE_BATCH
    unsigned int c = 0;
    PacketBatch* head = 0;
    Packet* last = 0;
#endif
    while (_queue_current != 0 && n < _burst) {
        Packet* p = _queue_current;

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
                if (_quick_clone)
                    SET_PAINT_ANNO(head->first(),PAINT_ANNO(p));
                last = head->first();
                c = 1;
            } else {
                //If next packet is for another output, send the pending batch and start a new one
                if (PAINT_ANNO(p) != PAINT_ANNO(head->first())) {
                    output_push_batch(PAINT_ANNO(head->first()),head->make_tail(last,c));
                    head = PacketBatch::start_head(q);
                    if (_quick_clone)
                        SET_PAINT_ANNO(head->first(),PAINT_ANNO(p));
                    last = head->first();
                    c = 1;
                } else {
                    //Just add the packet to the end of the batch
                    last->set_next(q);
                    last = q;
                    c++;
                }
            }
#else
            output(PAINT_ANNO(p)).push(q);
#endif
        n++;
    }

#if HAVE_BATCH
    //Flush pending batch
    if (head)
        output_push_batch(PAINT_ANNO(head->first()),head->make_tail(last,c));
#endif


    check_end_loop(task, n == 0);

    return n > 0;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(MultiReplay)
ELEMENT_MT_SAFE(MultiReplay)
EXPORT_ELEMENT(MultiReplayUnqueue)
ELEMENT_MT_SAFE(MultiReplayUnqueue)
