// -*- c-basic-offset: 4 -*-
#ifndef CLICK_CYCLECOUNTACCUM_HH
#define CLICK_CYCLECOUNTACCUM_HH

/*
=c
CycleCountAccum()

=s counters

collects differences in cycle counters

=d

Incoming packets should have their cycle counter annotation set.  Measures the
current value of the cycle counter, and keeps track of the total accumulated
difference.  Packets whose cycle counter annotations are zero are not added to
the count or difference.

=n

A packet has room for either exactly one cycle count or exactly one
performance metric.

=h count read-only

Returns the number of packets that have passed.

=h cycles read-only

Returns the accumulated cycles for all passing packets.

=h zero_count read-only

Returns the number of packets with zero-valued cycle counter annotations that
have passed.  These aren't included in the C<count>.

=h reset_counts write-only

Resets C<count>, C<cycles>, and C<zero_count> counters to zero when written.

=a SetCycleCount, RoundTripCycleCount, SetPerfCount, PerfCountAccum */

#include <click/batchelement.hh>
#include <click/sync.hh>

class CycleCountAccum : public SimpleElement<CycleCountAccum> { public:

    CycleCountAccum() CLICK_COLD;
    ~CycleCountAccum() CLICK_COLD;

    const char *class_name() const	{ return "CycleCountAccum"; }
    const char *port_count() const	{ return PORTS_1_1; }

    void add_handlers() override CLICK_COLD;

    inline Packet* simple_action(Packet *);

  private:

    struct state {
	state() : accum(0), count(0), zero_count(0) {

	}
	uint64_t accum;
	uint64_t count;
	uint64_t zero_count;
    };
    per_thread<state> _state;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int reset_handler(const String &, Element*, void*, ErrorHandler*);

};

#endif
