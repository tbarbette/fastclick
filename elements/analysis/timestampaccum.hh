// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TIMESTAMPACCUM_HH
#define CLICK_TIMESTAMPACCUM_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
=c

TimestampAccum()

=s timestamps

collects differences in timestamps

=d

For each passing packet, measures the elapsed time since the packet's
timestamp. Keeps track of the total elapsed time accumulated over all packets.

=h count read-only
Returns the number of packets that have passed.

=h time read-only
Returns the accumulated timestamp difference for all passing packets in seconds.

=h min read-only
Returns the minimal timestamp difference across all passing packets in seconds.

=h max read-only
Returns the maximal timestamp difference across all passing packets in seconds.

=h average_time read-only
Returns the average timestamp difference over all passing packets.

=h reset_counts write-only
Resets C<count> and C<time> counters to zero when written.

=a SetCycleCount, RoundTripCycleCount, SetPerfCount, PerfCountAccum */

class TimestampAccum : public SimpleElement<TimestampAccum> { public:

    TimestampAccum() CLICK_COLD;
    ~TimestampAccum() CLICK_COLD;

    const char *class_name() const override	{ return "TimestampAccum"; }
    const char *port_count() const override	{ return PORTS_1_1; }

    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    Packet *simple_action(Packet *);

  private:

    double _usec_accum;
    uint64_t _count;
    double _min;
    double _max;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int reset_handler(const String &, Element *, void *, ErrorHandler *);

};

CLICK_ENDDECLS
#endif
