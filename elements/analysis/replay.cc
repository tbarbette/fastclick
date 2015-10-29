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

Replay::Replay() : _loaded(false),_task(this), _burst(64)
{
	in_batch_mode = BATCH_MODE_YES;
}

Replay::~Replay()
{
}

int
Replay::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	//.read("TIME", _simple)
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

PacketBatch* Replay::pull_batch(int port, unsigned max) {
	PacketBatch* head;

	_task.reschedule();
	//click_chatter("Pull batch %d",port);
    MAKE_BATCH(_output[port].ring.extract(),head,max);

    /*if (head)
    	click_chatter("%d : Pull batch %p of %d packets",port,head,head->count());*/
    return head;
}

int
Replay::initialize(ErrorHandler *errh) {
	_notifier.initialize(Notifier::EMPTY_NOTIFIER, router());
	_notifier.set_active(false,false);
	_input.resize(ninputs());
	for (int i = 0 ; i < ninputs(); i++)
		_input[i].signal = Notifier::upstream_empty_signal(this, 0, (Task*)NULL);
	_output.resize(noutputs());
	for (int i = 0; i < _output.size(); i++) {
		_output[i].ring.initialize(1024);
	}
}

bool
Replay::run_task(Task* task)
{
	if (unlikely(!_loaded)) {
		Packet* p_input[ninputs()] = {0};
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
					p_input[i] = input(i).pull_batch(1);
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
				queue_tail = p_input[first_i];
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
		_queue_current = p->next();
		//p->get();
		//click_chatter("Insert in %d",PAINT_ANNO(p));
		Packet* q = p->clone(true);

		if (!_output[PAINT_ANNO(p)].ring.insert(q)) {
			q->kill();
			_queue_current = p;
			_notifier.sleep();
			return n > 0;
		} else {
			_notifier.wake();
		}

		n++;
	}
	if (unlikely(!_queue_current)) {
		_queue_current = _queue_head,
		click_chatter("Replay finished");
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
