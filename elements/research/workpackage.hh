// -*- c-basic-offset: 4 -*-
#ifndef CLICK_WORKPACKAGE_HH
#define CLICK_WORKPACKAGE_HH
#include <click/batchelement.hh>
#include "../standard/counter.hh"
#include <random>

CLICK_DECLS

/*
=c

WorkPackage(S, N, R, PAYLOAD, [W, ANALYSIS])

=s research

Artificially create CPU/Memory load

=d

Compute a random number for a certain amount of W time, and makes N accesses to
an array of size S (MB)

Keyword arguments are:

=over 8

=item S

Integer. Size of the memory array, per-CPU

=item N

Integer. Number of random access to the array per packet

=item R

Integer. Percentage of accesses made to the packet content instead of the array.

=item PAYLOAD

Boolean. If true, access the full payload of the packet, if false, access only its header. If R=0 this value is meaningless.

=item W

Integer. Number of times a pseudo-random number is generated per-packet before doing any access. This is to generate a constant amount of CPU load per packet.

=item ANALYSIS

Boolean. If true, track statistics about cycles, readable through handlers.

=back

Only available in user-level processes.

=n

=h cycles read-only

If ANALYSIS is true, gives the total number of cycles spent generating CPU load

=h cycles_per_work read-only

If ANALYSIS is true, gives the total number of cycles per W

=h cycles_work read-only

If ANALYSIS is true, gives the total number of W rounds

=a RandLoad
*/

class WorkPackage : public BatchElement { public:

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
