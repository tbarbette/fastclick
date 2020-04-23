// -*- c-basic-offset: 4 -*-
#ifndef CLICK_WORKPACKAGE_HH
#define CLICK_WORKPACKAGE_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
#include <random>

CLICK_DECLS

/*
=c

WorkPackage(W, N)

=s test

Compute a random number for a certain amount of W time, and makes N accesses to
an array of size S (MB)

*/

class WorkPackage : public BatchElement { public:

    WorkPackage() CLICK_COLD;

    const char *class_name() const      { return "WorkPackage"; }
    const char *port_count() const    { return "1-/="; }
    const char *processing() const    { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) override CLICK_COLD;
    void rmaction(Packet* p, int&);
    void push(int, Packet* p) override;
#if HAVE_BATCH
    void push_batch(int, PacketBatch* batch) override;
#endif

    void add_handlers() override CLICK_COLD;
private:
    int _r;
    int _n;
    int _w;
    bool _payload;
    Vector<uint32_t> _array;
    per_thread<std::mt19937> _gens;
    bool _analysis;
    struct WPStat {
	WPStat() : cycles_count(0), cycles_n(0) {

	};

	uint64_t cycles_count;
	uint64_t cycles_n;
    };
    per_thread<WPStat> _stats;




    static String read_handler(Element *, void *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
