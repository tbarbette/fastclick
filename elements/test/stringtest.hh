// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STRINGTEST_HH
#define CLICK_STRINGTEST_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

StringTest()

=s test

runs regression tests for Bitvector

=d

StringTest runs Bitvector regression tests at initialization time. It
does not route packets.

*/

class StringTest : public Element { public:

    StringTest() CLICK_COLD;

    const char *class_name() const		{ return "StringTest"; }

    int initialize(ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
