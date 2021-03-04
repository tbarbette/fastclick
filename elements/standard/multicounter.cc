/*
 * multicounter.{cc,hh} -- element maintains packet statistics
 *
 * Computational batching support
 * by Georgios Katsikas
 *
 * Copyright (c) 2017 KTH Royal Institute of Technology
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
#include "multicounter.hh"
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/args.hh>
#include <click/straccum.hh>
CLICK_DECLS

MultiCounter::MultiCounter() :
_count(0),
_byte_count(0),
_rate(0),
_byte_rate(0)
{
}

MultiCounter::~MultiCounter()
{
}


void
MultiCounter::cleanup(CleanupStage) {
	delete[] _count;
	delete[] _byte_count;
	delete[] _rate;
	delete[] _byte_rate;
}

void
MultiCounter::reset()
{
	for (int i=0; i<ninputs(); i++) {
		_count[i] = 0;
		_byte_count[i] = 0;
	}
}

void
MultiCounter::update(Packet *p, int port)
{
	_count[port]++;
	_byte_count[port] += p->length();
	_rate[port].update(1);
	_byte_rate[port].update(p->length());
}

int
MultiCounter::initialize(ErrorHandler *errh)
{
	_count = new counter_t[ninputs()];
	_byte_count = new counter_t[ninputs()];
	_rate = new rate_t[ninputs()];
	_byte_rate = new byte_rate_t[ninputs()];
	if (!_count || !_byte_count || !_rate || !_byte_rate ) {
		return errh->error("Out of memory!");
	}
	reset();
	return 0;
}

void
MultiCounter::push(int port, Packet *p)
{
	update(p, port);
	output(port).push(p);
}

#if HAVE_BATCH
void
MultiCounter::push_batch(int port, PacketBatch *b)
{
	FOR_EACH_PACKET(b, p) {
		update(p, port);
	}
	output_push_batch(0, b);
}
#endif

String
MultiCounter::format_counts(counter_t *counts, int size)
{
	StringAccum sa;
	sa <<'[';
	for (int i=0; i<size; i++) {
		sa << counts[i];
		if (i < size -1) {
			sa <<',';
		}
	}
	sa <<']';

	return sa.take_string();
}

String
MultiCounter::format_rates(rate_t *rates, int size)
{
	StringAccum sa;
	sa <<'[';
	for (int i=0; i<size; i++) {
		rates[i].update(0); // drop rate after idle period
		sa << rates[i].unparse_rate();
		if (i < size -1) {
			sa <<',';
		}
	}
	sa <<']';

	return sa.take_string();
}

String
MultiCounter::format_byte_rates(byte_rate_t *byte_rates, int size)
{
	StringAccum sa;
	sa <<'[';
	for (int i=0; i<size; i++) {
		byte_rates[i].update(0); // drop rate after idle period
		sa << byte_rates[i].unparse_rate();
		if (i < size -1) {
			sa <<',';
		}
	}
	sa <<']';

	return sa.take_string();
}

enum { H_COUNT, H_BYTE_COUNT, H_RATE, H_BYTE_RATE, H_ALL, H_RESET};

String
MultiCounter::read_handler(Element *e, void *thunk)
{
		MultiCounter *c = (MultiCounter *)e;
		switch ((intptr_t)thunk) {
			case H_COUNT:
				return format_counts(c->_count, c->ninputs());
			case H_BYTE_COUNT:
				return format_counts(c->_byte_count, c->ninputs());
			case H_RATE:
				return format_rates(c->_rate, c->ninputs());
			case H_BYTE_RATE:
				return format_byte_rates(c->_byte_rate, c->ninputs());
			default:
				return "<error>";
		}
}

int
MultiCounter::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
		MultiCounter *c = (MultiCounter *)e;
		String str = in_str;
		switch ((intptr_t)thunk) {
			case H_RESET:
				 c->reset();
				 return 0;
			default:
		return errh->error("<internal>");
		}
}

void
MultiCounter::add_handlers()
{
		add_read_handler("count", read_handler, H_COUNT);
		add_read_handler("byte_count", read_handler, H_BYTE_COUNT);
		add_read_handler("rate", read_handler, H_RATE);
		add_read_handler("byte_rate", read_handler, H_BYTE_RATE);
		add_write_handler("reset_counts", write_handler, H_RESET, Handler::f_button);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(MultiCounter)
