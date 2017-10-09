/*
 * FlowIDSMatcher.{cc,hh} -- element classifies packets by contents
 * using regular expression matching
 *
 * Element originally imported from http://www.openboxproject.org/
 *
 * Computational batching support by Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
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
#include <click/glue.hh>
#include <click/error.hh>
#include <click/args.hh>
#include <click/confparse.hh>
#include <click/router.hh>
#include "flowidsmatcher.hh"

CLICK_DECLS

FlowIDSMatcher::FlowIDSMatcher() : _program()
{
}

FlowIDSMatcher::~FlowIDSMatcher() {
}

int
FlowIDSMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool payload_only = false;
	if (Args(this, errh).bind(conf)
	  .consume() < 0)
	  return -1;

	for (int i=0; i < conf.size(); ++i) {
		String pattern = cp_unquote(conf[i]);
		int result = _program.add_pattern(pattern);
		if (result != 0) {
			// This should not happen
			return errh->error("Error (%d) adding pattern %d: %s", result, i, pattern.c_str());
		}
	}
	return 0;
}



int FlowIDSMatcher::process_data(fcb_FlowIDSMatcher* fcb_data, FlowBufferContentIter& iterator) {
    SimpleDFA::state_t state = fcb_data->state;
    if (state == SimpleDFA::MATCHED)
        return -1;

    FlowBufferContentIter good_packets(iterator);

    while (iterator) {
        unsigned char c = *iterator;
        _program.next(c,state);
        if (state == SimpleDFA::MATCHED) {
            return 1;
        } else if (state == 0) {
            good_packets = iterator;
        }
        ++iterator;
    }
    if (state != 0) {
        iterator = ++good_packets;
        if (good_packets.current()) {
        }
    }
    fcb_data->state = state;
    return 0;
}


String
FlowIDSMatcher::read_handler(Element *e, void *thunk)
{
	FlowIDSMatcher *c = (FlowIDSMatcher *)e;
	switch ((intptr_t)thunk) {
	  default:
		  return "<error>";
	}
}

int
FlowIDSMatcher::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
	FlowIDSMatcher *c = (FlowIDSMatcher *)e;
	switch ((intptr_t)thunk) {
		default:
			return errh->error("<internal>");
	}
}

void
FlowIDSMatcher::add_handlers() {
	for (uintptr_t i = 0; i != (uintptr_t) noutputs(); ++i) {
		add_read_handler("pattern" + String(i), read_positional_handler, (void*) i);
		add_write_handler("pattern" + String(i), reconfigure_positional_handler, (void*) i);
	}
}


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowIDSMatcher)
ELEMENT_REQUIRES(userlevel RegexSet)
ELEMENT_MT_SAFE(FlowIDSMatcher)
