// -*- c-basic-offset: 4 -*-
#ifndef CLICK_COUNTERTEST_HH
#define CLICK_COUNTERTEST_HH
#include <click/batchelement.hh>
CLICK_DECLS

class CounterBase;

/*
=c

CounterTest()

=s test

Call read_atomic on the counter in a certain proportion regarding the batch
it passes.

*/

class CounterTest : public BatchElement { public:

    CounterTest() CLICK_COLD;

    const char *class_name() const      { return "CounterTest"; }
    const char *port_count() const    { return PORTS_1_1; }
    const char *processing() const    { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) override;
    //void run_task(Task *) override;
    void push_batch(int, PacketBatch* batch);
private:
    CounterBase* _counter;
    int _rate;
    bool _atomic;

};

CLICK_ENDDECLS
#endif
