/*
 * regexmatcher.{cc,hh} -- element classifies packets by contents
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
#include "regexmatcher.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/args.hh>
#include <click/confparse.hh>
#include <click/router.hh>

CLICK_DECLS

RegexMatcher::RegexMatcher() : _payload_only(false), _match_all(false)
{
}

RegexMatcher::~RegexMatcher() {
}

int
RegexMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
{
	bool payload_only = false;
	bool match_all = false;
	if (Args(this, errh).bind(conf)
	  .read("PAYLOAD_ONLY", payload_only)
	  .read("MATCH_ALL", match_all)
	  .consume() < 0)
	  return -1;
	_payload_only = payload_only;
	_match_all = match_all;

	if (!is_valid_patterns(conf, errh)) {
		return -1;
	}

	if (!_program.is_open()) {
		_program.reset();
	}

	for (int i=0; i < conf.size(); ++i) {
		String pattern = cp_unquote(conf[i]);
		int result = _program.add_pattern(pattern);
		if (result < 0) {
			// This should not happen
			return errh->error("Error (%d) adding pattern %d: %s", result, i, pattern.c_str());
		}
	}

	if (!_program.compile()) {
		// This should not happen
		return errh->error("Unable to compile patterns");
	}


	if (!errh->nerrors()) {
		return 0;
	} else {
		return -1;
	}
}

bool
RegexMatcher::is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const{
	RegexSet test_set;
	bool valid = true;
	for (int i=0; i < patterns.size(); ++i) {
		String pattern = cp_unquote(patterns[i]);
		int result = test_set.add_pattern(pattern);
		if (result < 0) {
			errh->error("Error (%d) in pattern %d: %s", result, i, pattern.c_str());
			valid = false;
		}
	}
	if (valid) {
		// Try to compile
		valid = test_set.compile();
	}

	return valid;
}

int
RegexMatcher::find_output(Packet* p) {
	char* data = (char *) p->data();
	int length = p->length();
	if (_payload_only) {
		if (p->has_network_header()) {
			data = (char *) p->network_header();
			length = p->network_length();
		}
		if (p->has_transport_header()) {
			data = (char *) p->transport_header();
			length = p->transport_length();
		}
	}

	if (_match_all) {
		if (_program.match_all(data, length)) {
		    return 0;
		} else {
			return 1;
		}
	} else {
		if (_program.match_any(data, length)) {
		    return 0;
		} else {
		    return 1;
		}
	}
}

void
RegexMatcher::push(int, Packet* p) {
    checked_output_push(find_output(p), p);
}

#if HAVE_BATCH
void
RegexMatcher::push_batch(int, PacketBatch* batch) {
    CLASSIFY_EACH_PACKET(2,find_output,batch,checked_output_push_batch);
}
#endif

enum { H_PAYLOAD_ONLY, H_MATCH_ALL};

String
RegexMatcher::read_handler(Element *e, void *thunk)
{
	RegexMatcher *c = (RegexMatcher *)e;
	switch ((intptr_t)thunk) {
	  case H_PAYLOAD_ONLY:
		  return String(c->_payload_only);
	  case H_MATCH_ALL:
		  return String(c->_match_all);
	  default:
		  return "<error>";
	}
}

int
RegexMatcher::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
	RegexMatcher *c = (RegexMatcher *)e;
	switch ((intptr_t)thunk) {
		case H_PAYLOAD_ONLY:
		   if (!BoolArg().parse(in_str, c->_payload_only))
				return errh->error("syntax error");
		   return 0;
		case H_MATCH_ALL:
			if (!BoolArg().parse(in_str, c->_match_all))
				return errh->error("syntax error");
		   return 0;
		default:
			return errh->error("<internal>");
	}
}

void
RegexMatcher::add_handlers() {
	for (uintptr_t i = 0; i != (uintptr_t) noutputs(); ++i) {
		add_read_handler("pattern" + String(i), read_positional_handler, (void*) i);
		add_write_handler("pattern" + String(i), reconfigure_positional_handler, (void*) i);
	}
	add_read_handler("payload_only", read_handler, H_PAYLOAD_ONLY);
	add_read_handler("match_all", read_handler, H_MATCH_ALL);
	add_write_handler("payload_only", write_handler, H_PAYLOAD_ONLY);
	add_write_handler("match_all", write_handler, H_MATCH_ALL);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(RegexMatcher)
ELEMENT_REQUIRES(userlevel RegexSet)
ELEMENT_MT_SAFE(RegexMatcher)
