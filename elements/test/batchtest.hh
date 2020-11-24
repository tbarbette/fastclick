// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BATCHTEST_HH
#define CLICK_BATCHTEST_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

BatchTest()

=s test

Displays push packet or push batch depending on the type of function called

*/

class BatchTest : public BatchElement { public:

    BatchTest() CLICK_COLD;

    const char *class_name() const override		{ return "BatchTest"; }
    const char *port_count() const override    { return PORTS_1_1; }
    const char *processing() const override    { return AGNOSTIC; }

    void push(int, Packet *) override;
    void push_batch(int, PacketBatch *) override;
    Packet* pull(int) override;
    PacketBatch* pull_batch(int, unsigned) override;
};

/*
=c

BatchTest()

=s test

Displays push packet or push batch depending on the type of function called

*/

class BatchElementTest : public Element { public:

    BatchElementTest() CLICK_COLD;

    const char *class_name() const override      { return "BatchElementTest"; }
    const char *port_count() const override    { return PORTS_1_1; }
    const char *processing() const override    { return AGNOSTIC; }

    void push(int, Packet *);
    void push_batch(int, PacketBatch *);
};

CLICK_ENDDECLS
#endif
