/*
 * ctxidsmatcher.{cc,hh} -- element classifies packets by contents
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
#include "ctxidsmatcher.hh"

CLICK_DECLS

CTXIDSMatcher::CTXIDSMatcher() : _program(),_stall(false)
{
    _stalled = 0;
    _matched = 0;
}

CTXIDSMatcher::~CTXIDSMatcher() {
}

int
CTXIDSMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool payload_only = false;
	if (Args(this, errh).bind(conf)
	        .read("STALL",_stall)
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



int CTXIDSMatcher::process_data(fcb_CTXIDSMatcher* fcb_data, FlowBufferContentIter& iterator) {
    SimpleDFA::state_t state = fcb_data->state;
    if (state == SimpleDFA::MATCHED)
        return -1;

    FlowBufferContentIter good_packets(iterator);

    while (iterator) {
        unsigned char c = *iterator;
        _program.next(c,state);
        if (unlikely(state == SimpleDFA::MATCHED)) {
            _matched ++;
            return 1;
        } else if (_stall && state == 0) {
            good_packets = iterator;
        }
        ++iterator;
    }
    if (_stall && state != 0) {
        _stalled++;
        iterator = ++good_packets;
        if (good_packets.current()) {
            //TODO
        }
    }
    fcb_data->state = state;
    return 0;
}


String
CTXIDSMatcher::read_handler(Element *e, void *thunk)
{
	CTXIDSMatcher *c = (CTXIDSMatcher *)e;
	switch ((intptr_t)thunk) {
	  default:
		  return "<error>";
	}
}

int
CTXIDSMatcher::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
	CTXIDSMatcher *c = (CTXIDSMatcher *)e;
	switch ((intptr_t)thunk) {
		default:
			return errh->error("<internal>");
	}
}

void
CTXIDSMatcher::add_handlers() {
	add_data_handlers("stalled", Handler::h_read, &_stalled);
	add_data_handlers("matched", Handler::h_read, &_matched);
}


//Chunk

FlowIDSChunkMatcher::FlowIDSChunkMatcher() : _program()
{
    _stalled = 0;
    _matched = 0;
}

FlowIDSChunkMatcher::~FlowIDSChunkMatcher() {
}

int
FlowIDSChunkMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
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



int
FlowIDSChunkMatcher::process_data(fcb_CTXIDSMatcher* fcb_data, FlowBufferChunkIter& iterator) {
    SimpleDFA::state_t state = fcb_data->state;
    if (state == SimpleDFA::MATCHED)
        return -1;

    while (iterator) {
        Chunk ch = *iterator;
        _program.next_chunk(ch.bytes,ch.length,state);
        if (unlikely(state == SimpleDFA::MATCHED)) {
            _matched ++;
            return 1;
        }
        ++iterator;
    }
    fcb_data->state = state;
    return 0;
}


String
FlowIDSChunkMatcher::read_handler(Element *e, void *thunk)
{
    CTXIDSMatcher *c = (CTXIDSMatcher *)e;
    switch ((intptr_t)thunk) {
      default:
          return "<error>";
    }
}

int
FlowIDSChunkMatcher::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
    CTXIDSMatcher *c = (CTXIDSMatcher *)e;
    switch ((intptr_t)thunk) {
        default:
            return errh->error("<internal>");
    }
}

void
FlowIDSChunkMatcher::add_handlers() {
    add_data_handlers("stalled", Handler::h_read, &_stalled);
    add_data_handlers("matched", Handler::h_read, &_matched);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(CTXIDSMatcher)
EXPORT_ELEMENT(FlowIDSChunkMatcher)
ELEMENT_REQUIRES(userlevel)
ELEMENT_MT_SAFE(CTXIDSMatcher)
ELEMENT_MT_SAFE(FlowIDSChunkMatcher)
