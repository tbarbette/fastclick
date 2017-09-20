/*
 * stringmatcher.{cc,hh} -- element matches a packet based on a set of strings
 *
 * Computational batching support
 * by Georgios Katsikas and Tom Barbette
 *
 * Copyright (c) 2017 KTH Royal Institute of Technology
 * Copyright (c) 2017 University of Liege
 *
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
#include "stringmatcher.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/confparse.hh>
#include <click/router.hh>
CLICK_DECLS

StringMatcher::StringMatcher() : _matches(0) {
}

StringMatcher::~StringMatcher() {
}

int
StringMatcher::configure(Vector<String> &conf, ErrorHandler *errh)
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
        if (_matcher.add_pattern(conf[i], i)) {
            errh->warning("Pattern #d is a duplicate");
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

int
StringMatcherMP::configure(Vector<String> &conf, ErrorHandler *errh)
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

    for (int i = 0; i < _matchers.weight(); i ++) {
        auto &m =_matchers.get_value(i);
        for (int i=0; i < conf.size(); ++i) {
            // All patterns should be OK so we can only have duplicates
            if (m.add_pattern(conf[i], i)) {
                errh->warning("Pattern #d is a duplicate");
            } else {
                _patterns.push_back(conf[i]);
            }
        }
        m.finalize();
    }

    if (!errh->nerrors()) {
        return 0;
    } else {
        return -1;
    }
}



bool
StringMatcher::is_valid_patterns(Vector<String> &patterns, ErrorHandler *errh) {
    bool valid = true;
    AhoCorasick matcher;
    for (int i=0; i<patterns.size(); ++i) {
        AhoCorasick::EnumReturnStatus rv = matcher.add_pattern(patterns[i], i);
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

inline int
StringMatcher::smaction(Packet *p) {
    if (_matcher.match_any(p, false)) {
        _matches++;
        return 1;
    } else {
        // push to port 0
        return 0;
    }
}


Packet *
StringMatcher::simple_action(Packet *p) {
    checked_output_push(smaction(p),p);
}


#if HAVE_BATCH
PacketBatch *
StringMatcher::simple_action_batch(PacketBatch *batch)
{
    CLASSIFY_EACH_PACKET(2, smaction, batch, checked_output_push_batch);
}
#endif

inline int
StringMatcherMP::smaction(Packet *p) {
    if (_matchers->match_any(p, false)) {
        _matches++;
        return 1;
    } else {
        // push to port 0
        return 0;
    }
}

Packet *
StringMatcherMP::simple_action(Packet *p) {
    checked_output_push(smaction(p),p);
}

#if HAVE_BATCH
PacketBatch *
StringMatcherMP::simple_action_batch(PacketBatch *batch)
{
    CLASSIFY_EACH_PACKET(2, smaction, batch, checked_output_push_batch);
}
#endif

int
StringMatcher::write_handler(const String &, Element *e, void *, ErrorHandler *) {
    StringMatcher * string_matcher = static_cast<StringMatcher *>(e);
    string_matcher->_matches = 0;
    return 0;
}

void
StringMatcher::add_handlers() {
    add_data_handlers("matches", Handler::h_read, &_matches);
    add_write_handler("reset_count", write_handler, 0, Handler::h_button | Handler::h_nonexclusive);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel AhoCorasick)
EXPORT_ELEMENT(StringMatcher)
EXPORT_ELEMENT(StringMatcherMP)
ELEMENT_MT_SAFE(StringMatcherMP)
