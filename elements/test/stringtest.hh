// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STRINGTEST_HH
#define CLICK_STRINGTEST_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

StringTest([keywords])

=s test

runs regression tests for String

=d

StringTest runs String regression tests at initialization time. It
does not route packets.

*/

class StringTest : public Element { public:

    StringTest() CLICK_COLD;

    const char *class_name() const override		{ return "StringTest"; }

    int initialize(ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
