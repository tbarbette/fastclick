// -*- c-basic-offset: 4 -*-
#ifndef CLICK_WORKPACKAGE_HH
#define CLICK_WORKPACKAGE_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
#include <random>

CLICK_DECLS

/*
=c

WorkPackage([S, N, R, PAYLOAD, W])

=s test

Compute W random numbers, then makes N accesses to an array of S bytes if R = 0 for every packets
passing by.
If R is bigger than 0, R% of the accesses will be made to the payload of the packet
instead of the array of bytes.

*/
class WorkPackage : public BatchElement {
    public:
        WorkPackage() CLICK_COLD;

        const char *class_name() const override { return "WorkPackage"; }
        const char *port_count() const override { return "1-/="; }
        const char *processing() const override { return PUSH; }

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

        static std::random_device rd;
};

CLICK_ENDDECLS
#endif
