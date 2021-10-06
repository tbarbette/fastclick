/*
 * stringclassifier.{cc,hh} -- element classifies packets by contents
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
#include "stringclassifier.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/router.hh>
CLICK_DECLS

StringClassifier::StringClassifier() : _matches(0) {
}

StringClassifier::~StringClassifier() {
}

int
StringClassifier::configure(Vector<String> &conf, ErrorHandler *errh)
{
	// This check will prevent us from doing any changes to the state if there is an error
	if (!is_valid_patterns(conf, errh)) {
		return -1;
	}

	// if we are reconfiguring we need to reset the patterns and the matcher
	if ((!_matcher.is_open()) || _patterns.size()) {
		_matcher.reset();
		_patterns.clear();
	}

	for (int i=0; i < conf.size(); ++i) {
		// All patterns should be OK so we can only have duplicates
		if (_matcher.add_pattern(cp_unquote(conf[i]), i)) {
			errh->warning("Pattern %d is a duplicate", i);
		} else {
			_patterns.push_back(conf[i]);
		}
	}

	_matcher.finalize();


	if (!errh->nerrors()) {
		return 0;
	} else {
		return -1;
	}
}

bool
StringClassifier::is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) const{
	bool valid = true;
	AhoCorasick matcher;
	for (int i=0; i<patterns.size(); ++i) {
		AhoCorasick::EnumReturnStatus rv = matcher.add_pattern(cp_unquote(patterns[i]), i);
		switch (rv) {
			case AhoCorasick::RETURNSTATUS_ZERO_PATTERN:
				errh->error("Pattern #%d has zero length", i);
				valid = false;
				break;
			case AhoCorasick::RETURNSTATUS_LONG_PATTERN:
				errh->error("Pattern #%d is too long", i);
				valid = false;
				break;
			case AhoCorasick::RETURNSTATUS_FAILED:
				errh->error("Pattern #%d had unknown error", i);
				valid = false;
				break;
			default:
				break;
		}
	}

	return valid;
}

int
StringClassifier::find_output(Packet *p) {
	int output = _matcher.match_first(p, false);
	if (output == -1) {
		output = _patterns.size();
	}
	return output;
}

void
StringClassifier::push(int, Packet *p) {
	int output = find_output(p);
	checked_output_push(output, p);
}

#if HAVE_BATCH
void
StringClassifier::push_batch(int port, PacketBatch *batch)
{
	unsigned short outports = noutputs();
	PacketBatch *out[outports];
	bzero(out,sizeof(PacketBatch*)*outports);
	Packet* next = ((batch != NULL)? batch->first()->next() : NULL );
	Packet* p = batch->first();
	Packet* last = NULL;
	int last_o = -1;
	int passed = 0;
	int count  = 0;

	for ( ;p != NULL; p=next,next=(p==0?0:p->next()) ) {
		// The actual job of this element
		int o = find_output(p);

		if (o < 0 || o>=(outports)) o = (outports - 1);

		if (o == last_o) {
			passed ++;
		}
		else {
			if ( !last ) {
				out[o] = PacketBatch::make_from_packet(p);
			}
			else {
				out[last_o]->set_tail(last);
				out[last_o]->set_count(out[last_o]->count() + passed);
				if (!out[o]) {
					out[o] = PacketBatch::make_from_packet(p);
				}
				else {
					out[o]->append_packet(p);
				}
				passed = 0;
			}
		}
		last = p;
		last_o = o;
		count++;
	}

	if (passed) {
		out[last_o]->set_tail(last);
		out[last_o]->set_count(out[last_o]->count() + passed);
	}

	int i = 0;
	for (; i < outports; i++) {
		if (out[i]) {
			out[i]->tail()->set_next(NULL);
			checked_output_push_batch(i, out[i]);
		}
	}
}
#endif

int
StringClassifier::write_handler(const String &, Element *e, void *, ErrorHandler *) {
	StringClassifier * string_matcher = static_cast<StringClassifier *>(e);
	string_matcher->_matches = 0;
	return 0;
}

void
StringClassifier::add_handlers() {
	add_data_handlers("matches", Handler::h_read, &_matches);
	add_write_handler("reset_count", write_handler, 0, Handler::h_button | Handler::h_nonexclusive);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel AhoCorasick)
EXPORT_ELEMENT(StringClassifier)
ELEMENT_MT_SAFE(StringClassifier)
