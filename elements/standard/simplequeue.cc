// -*- c-basic-offset: 4 -*-
/*
 * simplequeue.{cc,hh} -- queue element
 * Eddie Kohler
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
#include "simplequeue.hh"
#include <click/args.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/packet.hh>
CLICK_DECLS

SimpleQueue::SimpleQueue()
    : _q(0), _nouseless(false)
{
}

void *
SimpleQueue::cast(const char *n)
{
    if (strcmp(n, "Storage") == 0)
	return (Storage *)this;
    else if (strcmp(n, "SimpleQueue") == 0
	     || strcmp(n, "Queue") == 0)
	return (Element *)this;
    else
	return 0;
}

int
SimpleQueue::configure(Vector<String> &conf, ErrorHandler *errh)
{
    unsigned new_capacity = 1000;
    bool nouseless = false;
    if (Args(conf, this, errh)
            .read_p("CAPACITY", new_capacity)
            .read("NOUSELESS", nouseless)
            .complete() < 0)
	return -1;
    _capacity = new_capacity;
    _nouseless = nouseless;
    return 0;
}

int
SimpleQueue::initialize(ErrorHandler *errh)
{
    assert(!_q && head() == 0 && tail() == 0);
    _q = (Packet **) CLICK_LALLOC(sizeof(Packet *) * (_capacity + 1));
    if (_q == 0)
	return errh->error("out of memory");
    _drops = 0;
    _highwater_length = 0;

    Bitvector b = get_passing_threads();
    unsigned int thisthread = router()->home_thread_id(this);

    for (unsigned i = 0; i < (unsigned)b.size(); i++) {
       if (b[i] && i != thisthread) {
           WritablePacket::pool_transfer(thisthread,i);
       }
    }

    return 0;
}

bool
SimpleQueue::do_mt_safe_check(ErrorHandler* errh) {
    Bitvector b(master()->nthreads());
    bool fp;
    for (int i = 0; i < ninputs(); i++) {
        if (input_is_push(i))
            b |= get_passing_threads(false, i, this, fp, 0);
    }
    int ninput = b.weight();
    b.zero();
    for (int i = 0; i < noutputs(); i++) {
        if (output_is_pull(i))
            b |= get_passing_threads(true, i, this, fp, 0);
    }
    int noutput = b.weight();

    if (ninput <= 1 && noutput <= 1) {
        if (cast("ThreadSafeQueue") != 0 && !_nouseless) {
            errh->warning("Useless ThreadSafeQueue in a non-mt environment. Set NOUSELESS to true to avoid this warning if this is normal.");
        }
        return true;
    } else if (cast("ThreadSafeQueue") != 0) {
        return true;
    } else
        return errh->error("Queue %s is not multithread-safe ! Use ThreadSafeQueue instead.",class_name());
    return true;
}

bool
SimpleQueue::get_spawning_threads(Bitvector& b, bool, int port) {
    return false;
}

int
SimpleQueue::live_reconfigure(Vector<String> &conf, ErrorHandler *errh)
{
    // change the maximum queue length at runtime
    Storage::index_type old_capacity = _capacity;
    // NB: do not call children!
    if (SimpleQueue::configure(conf, errh) < 0)
	return -1;
    if (_capacity == old_capacity || !_q)
	return 0;
    Storage::index_type new_capacity = _capacity;
    _capacity = old_capacity;

    Packet **new_q = (Packet **) CLICK_LALLOC(sizeof(Packet *) * (new_capacity + 1));
    if (new_q == 0)
	return errh->error("out of memory");

    Storage::index_type i, j;
    for (i = head(), j = 0; i != tail() && j != new_capacity; i = next_i(i))
	new_q[j++] = _q[i];
    for (; i != tail(); i = next_i(i))
	_q[i]->kill();

    CLICK_LFREE(_q, sizeof(Packet *) * (_capacity + 1));
    _q = new_q;
    set_head(0);
    set_tail(j);
    _capacity = new_capacity;
    return 0;
}

void
SimpleQueue::take_state(Element *e, ErrorHandler *errh)
{
    SimpleQueue *q = (SimpleQueue *)e->cast("SimpleQueue");
    if (!q)
	return;

    if (tail() != head() || head() != 0) {
	errh->error("already have packets enqueued, can%,t take state");
	return;
    }

    set_head(0);
    Storage::index_type i = 0, j = q->head();
    while (i < _capacity && j != q->tail()) {
	_q[i] = q->_q[j];
	i++;
	j = q->next_i(j);
    }
    set_tail(i);
    _highwater_length = size();

    if (j != q->tail())
	errh->warning("some packets lost (old length %d, new capacity %d)",
		      q->size(), _capacity);
    while (j != q->tail()) {
	q->_q[j]->kill();
	j = q->next_i(j);
    }
    q->set_head(0);
    q->set_tail(0);
}

void
SimpleQueue::cleanup(CleanupStage)
{
    for (Storage::index_type i = head(); i != tail(); i = next_i(i))
	_q[i]->kill();
    CLICK_LFREE(_q, sizeof(Packet *) * (_capacity + 1));
    _q = 0;
}

#if HAVE_BATCH
void SimpleQueue::push_batch(int port, PacketBatch* batch) {
	FOR_EACH_PACKET_SAFE(batch,p) {
		push(port,p);
	}
}

PacketBatch* SimpleQueue::pull_batch(int port, unsigned max) {
	PacketBatch* batch;
	MAKE_BATCH(pull(port),batch,max);
	return batch;
}
#endif

inline void
SimpleQueue::push(int, Packet *p)
{
    // If you change this code, also change NotifierQueue::push()
    // and FullNoteQueue::push().
    Storage::index_type h = head(), t = tail(), nt = next_i(t);

    // should this stuff be in SimpleQueue::enq?
    if (nt != h) {
	_q[t] = p;
	set_tail(nt);

	int s = size(h, nt);
	if (s > _highwater_length)
	    _highwater_length = s;

    } else {
	// if (!(_drops % 100))
	if (_drops == 0 && _capacity > 0)
	    click_chatter("%p{element}: overflow", this);
	_drops++;
	checked_output_push(1, p);
    }
}

Packet *
SimpleQueue::pull(int)
{
    return deq();
}


String
SimpleQueue::read_handler(Element *e, void *thunk)
{
    SimpleQueue *q = static_cast<SimpleQueue *>(e);
    int which = reinterpret_cast<intptr_t>(thunk);
    switch (which) {
      case 0:
	return String(q->size());
      case 1:
	return String(q->highwater_length());
      case 2:
	return String(q->capacity());
      case 3:
	return String(q->_drops);
      default:
	return "";
    }
}

void
SimpleQueue::reset()
{
    while (Packet *p = pull(0))
	checked_output_push(1, p);
}

int
SimpleQueue::write_handler(const String &, Element *e, void *thunk, ErrorHandler *errh)
{
    SimpleQueue *q = static_cast<SimpleQueue *>(e);
    int which = reinterpret_cast<intptr_t>(thunk);
    switch (which) {
      case 0:
	q->_drops = 0;
	q->_highwater_length = q->size();
	return 0;
      case 1:
	q->reset();
	return 0;
      default:
	return errh->error("internal error");
    }
}

void
SimpleQueue::add_handlers()
{
    add_read_handler("length", read_handler, 0);
    add_read_handler("highwater_length", read_handler, 1);
    add_read_handler("capacity", read_handler, 2, Handler::h_calm);
    add_read_handler("drops", read_handler, 3);
    add_write_handler("capacity", reconfigure_keyword_handler, "0 CAPACITY");
    add_write_handler("reset_counts", write_handler, 0, Handler::h_button | Handler::h_nonexclusive);
    add_write_handler("reset", write_handler, 1, Handler::h_button);
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(Storage)
EXPORT_ELEMENT(SimpleQueue)
