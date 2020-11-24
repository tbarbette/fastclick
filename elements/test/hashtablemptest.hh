// -*- c-basic-offset: 4 -*-
#ifndef CLICK_HASHTABLEMPTEST_HH
#define CLICK_HASHTABLEMPTEST_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

HashTableMPTest()

=s test

runs regression tests for HashTable<K, V>

=d

HashTableMPTest runs HashTable regression tests at initialization time. It
does not route packets.

*/

class HashTableMPTest : public Element { public:

    HashTableMPTest() CLICK_COLD;

    const char *class_name() const override		{ return "HashTableMPTest"; }

    int initialize(ErrorHandler *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
