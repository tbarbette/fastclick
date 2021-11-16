// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TIMESTAMPACCUM_HH
#define CLICK_TIMESTAMPACCUM_HH
#include <click/batchelement.hh>
#include <click/straccum.hh>
#include <click/args.hh>
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
template <template <typename> class T>
class TimestampAccumBase : public BatchElement { public:

    TimestampAccumBase() CLICK_COLD;
    ~TimestampAccumBase() CLICK_COLD;

    const char *class_name() const	{ return "TimestampAccum"; }
    const char *port_count() const	{ return "1-/="; }

    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    void push(int, Packet *);

#if HAVE_BATCH
    void push_batch(int, PacketBatch *);
#endif

  protected:

    struct State {
        State() : nsec_accum(0), count(0), nsec_min(UINT64_MAX), nsec_max(0) {


        };
        uint64_t nsec_accum;
        uint64_t count;
        uint64_t nsec_min;
        uint64_t nsec_max;
    };
    T<State> _state;
    bool _steady;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int reset_handler(const String &, Element *, void *, ErrorHandler *);

};

typedef TimestampAccumBase<not_per_thread> TimestampAccum;

class TimestampAccumMP : public TimestampAccumBase<per_thread> { public:

    const char *class_name() const  { return "TimestampAccumMP"; }

    void add_handlers() CLICK_COLD;
    static String read_handler(Element *, void *) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
