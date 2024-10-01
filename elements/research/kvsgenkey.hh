// -*- c-basic-offset: 4 -*-
#ifndef CLICK_KVSKeyGen_HH
#define CLICK_KVSKeyGen_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
#include <random>

CLICK_DECLS

/*
=c

KVSKeyGen(W, N)

=s test

Compute a random number for a certain amount of W time, and makes N accesses to

*/
class KVSKeyGen : public SimpleElement<KVSKeyGen> {
    public:
        KVSKeyGen() CLICK_COLD;

        const char *class_name() const override { return "KVSKeyGen"; }
        const char *port_count() const override { return "1-/="; }
        const char *processing() const override { return PUSH; }

        int configure(Vector<String>&, ErrorHandler*) override;
        Packet* simple_action(Packet* p);

    private:
        
        per_thread<std::mt19937> _gens;
        static std::random_device rd;
        int _offset;
};

CLICK_ENDDECLS
#endif
