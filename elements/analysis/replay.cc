// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * replay.{cc,hh} -- replay some packets
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

Replay::Replay() : _active(true), _loaded(false), _queue(1024), _burst(64), _stop(-1), _quick_clone(false), _task(this), _queue_head(0), _queue_current(0)
{
#if HAVE_BATCH
	in_batch_mode = BATCH_MODE_YES;
#endif
}

Replay::~Replay()
{
}

int
Replay::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_p("QUEUE", _queue)
	.read("STOP", _stop)
	.read("QUICK_CLONE", _quick_clone)
	.complete() < 0)
    return -1;

    ScheduleInfo::initialize_task(this,&_task,true,errh);
    return 0;
}

void *
Replay::cast(const char *n)
{
    if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0)
	return static_cast<Notifier *>(&_notifier);
    else
	return Element::cast(n);
}
Packet* Replay::pull(int port) {
	_task.reschedule();
    return _output[port].ring.extract();
}

#if HAVE_BATCH
PacketBatch* Replay::pull_batch(int port, unsigned max) {
	PacketBatch* head;
	_task.reschedule();
    MAKE_BATCH(_output[port].ring.extract(),head,max);
    return head;
}
#endif

int
Replay::initialize(ErrorHandler *) {
	_notifier.initialize(Notifier::EMPTY_NOTIFIER, router());
	_notifier.set_active(false,false);
	_input.resize(ninputs());
	for (int i = 0 ; i < ninputs(); i++)
		_input[i].signal = Notifier::upstream_empty_signal(this, i, (Task*)NULL);
	_output.resize(noutputs());
	for (int i = 0; i < _output.size(); i++) {
		_output[i].ring.initialize(_queue);
	}
	return 0;
}

bool
Replay::run_task(Task* task)
{
	if (!_active)
		return false;

	if (unlikely(!_loaded)) {
		Packet* p_input[ninputs()];
		bzero(p_input,sizeof(Packet*) * ninputs());
		int first_i = -1;
		Timestamp first_t;
		Packet* queue_tail = 0;
		int count = 0;

		click_chatter("Loading %s with %d inputs.",name().c_str(),ninputs());
		//Dry is the index of the first input to dry out
		int dry = -1;
		do {
			for (int i = 0; i < ninputs(); i++) {
				if (p_input[i] == 0) {
					do_pull:
#if HAVE_BATCH
					p_input[i] = input_pull_batch(i,1);
#else
					p_input[i] = input(i).pull();
#endif
					if (p_input[i] == 0) {
						if (_input[i].signal.active()) goto do_pull;
						dry = i;
						break;
					}
				}

				Packet*& p = p_input[i];
				Timestamp t = p->timestamp_anno();
				if (i == 0) {
					first_i = 0;
					first_t = t;
				} else {
					if (t <  first_t) {
						first_i = i;
						first_t = t;
					}
				}
			}
			if (dry >= 0)
				break;
			if (!_queue_head) {
				_queue_head = p_input[first_i];
			} else {
				queue_tail->set_next(p_input[first_i]);
			}
			queue_tail = p_input[first_i];
			SET_PAINT_ANNO(p_input[first_i],first_i);
			p_input[first_i] = 0;
			count++;
			if (!router()->running())
				return 0;
		} while(dry < 0);

		click_chatter("Successfully loaded %d packets. Input %d dried out.",count,dry);

		//Clean left over
		for (int i = 0; i < ninputs(); i++) {
			if (p_input[i])
				p_input[i]->kill();
		}
		_loaded = true;
		_queue_current = _queue_head;
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
			if (_stop > 1) {
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

	if (unlikely(!_queue_current)) {
		_queue_current = _queue_head;
		if (_stop > 0)
			_stop--;
		if (_stop == 0) {
			router()->please_stop_driver();
			_active = false;
			return true;
		}
		click_chatter("Replay loop");
	}
	task->fast_reschedule();

	return n > 0;
}

void Replay::cleanup(CleanupStage) {
	while (_queue_head) {
		Packet* next = _queue_head->next();
		_queue_head->kill();
		_queue_head = next;
	}
}

/*
Packet*
Replay::pull(int port)
{

    return p;
}*/


CLICK_ENDDECLS
EXPORT_ELEMENT(Replay)
ELEMENT_MT_SAFE(Replay)
