// -*- c-basic-offset: 4 -*-
#ifndef CLICK_WorkPackage_HH
#define CLICK_WorkPackage_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
#include <random>

CLICK_DECLS

/*
=c

WorkPackage()

=s test

Call read_atomic on the counter in a certain proportion regarding the batch
it passes.

*/

class WorkPackage : public BatchElement { public:

    WorkPackage() CLICK_COLD;

    const char *class_name() const      { return "WorkPackage"; }
    const char *port_count() const    { return "1-/="; }
    const char *processing() const    { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) override;
    void rmaction(Packet* p, int&);
    void push(int, Packet* p) override;
#if HAVE_BATCH
    void push_batch(int, PacketBatch* batch) override;
#endif
private:
    int _r;
    int _n;
    int _w;
    bool _payload;
    Vector<uint32_t> _array;
    per_thread<std::mt19937> _gens;
};

CLICK_ENDDECLS
#endif
