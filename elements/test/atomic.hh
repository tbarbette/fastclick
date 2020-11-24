// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ATOMICTEST_HH
#define CLICK_ATOMICTEST_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

AtomicTest()

=s test

Test atomic variables implementation

=d

This element routes no packets and does all its work at initialization time.

*/

class AtomicTest : public Element { public:

    AtomicTest() CLICK_COLD;

    const char *class_name() const		{ return "AtomicTest"; }

    int initialize(ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
