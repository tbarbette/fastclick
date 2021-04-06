// -*- c-basic-offset: 4 -*-
#ifndef CLICK_DevirtualizeTest_HH
#define CLICK_DevirtualizeTest_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

DevirtualizeTest()

=s test

runs regression tests for Vector

=d

DevirtualizeTest runs Vector regression tests at initialization time. It
does not route packets.

*/

class DevirtualizeTest : public Element { public:

    DevirtualizeTest() CLICK_COLD;

    const char *class_name() const override		{ return "DevirtualizeTest"; }

    int initialize(ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
