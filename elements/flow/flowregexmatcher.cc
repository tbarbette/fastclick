/*
 * FlowRegexMatcher.{cc,hh} -- element classifies packets by contents
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
#include "flowregexmatcher.hh"

CLICK_DECLS

FlowRegexMatcher::FlowRegexMatcher() : _program()
{
}

FlowRegexMatcher::~FlowRegexMatcher() {
}

int
FlowRegexMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
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

    return 0;;
}



int FlowRegexMatcher::process_data(fcb_FlowRegexMatcher* fcb_data, FlowBufferContentIter& iterator) {
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


String
FlowRegexMatcher::read_handler(Element *e, void *thunk)
{
	FlowRegexMatcher *c = (FlowRegexMatcher *)e;
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
FlowRegexMatcher::write_handler(const String &in_str, Element *e, void *thunk, ErrorHandler *errh)
{
	FlowRegexMatcher *c = (FlowRegexMatcher *)e;
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
FlowRegexMatcher::add_handlers() {
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
//EXPORT_ELEMENT(FlowRegexMatcher)
ELEMENT_REQUIRES(userlevel RegexSet)
//ELEMENT_MT_SAFE(FlowRegexMatcher)
