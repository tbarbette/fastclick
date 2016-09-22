// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_COUNTER_HH
#define CLICK_COUNTER_HH
#include <click/element.hh>
#include <click/ewma.hh>
#include <click/llrpc.h>
#include <click/sync.hh>
#include <click/multithread.hh>
CLICK_DECLS
class HandlerCall;

#ifdef HAVE_INT64_TYPES
#define counter_sum per_thread_arithmetic::usum_long
#define counter_set per_thread_arithmetic::uset_long
#define counter_int_type uint64_t
#else
#define counter_sum per_thread_arithmetic::usum
#define counter_set per_thread_arithmetic::uset
#define counter_int_type uint32_t
#endif

/*
=c

Counter([I<keywords COUNT_CALL, BYTE_COUNT_CALL>])

=s counters

measures packet count and rate

=d

Passes packets unchanged from its input to its output, maintaining statistics
information about packet count and packet rate.

Keyword arguments are:

=over 8

=item COUNT_CALL

Argument is `I<N> I<HANDLER> [I<VALUE>]'. When the packet count reaches I<N>,
call the write handler I<HANDLER> with value I<VALUE> before emitting the
packet.

=item BYTE_COUNT_CALL

Argument is `I<N> I<HANDLER> [I<VALUE>]'. When the byte count reaches or
exceeds I<N>, call the write handler I<HANDLER> with value I<VALUE> before
emitting the packet.

=back

=h count read-only

Returns the number of packets that have passed through since the last reset.

=h byte_count read-only

Returns the number of bytes that have passed through since the last reset.

=h rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in packets per second.

=h bit_rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in bits per second.

=h byte_rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in bytes per second.

=h reset_counts write-only

Resets the counts and rates to zero.

=h reset write-only

Same as 'reset_counts'.

=h count_call write-only

Writes a new COUNT_CALL argument. The handler can be omitted.

=h byte_count_call write-only

Writes a new BYTE_COUNT_CALL argument. The handler can be omitted.

=h CLICK_LLRPC_GET_RATE llrpc

Argument is a pointer to an integer that must be 0.  Returns the recent
arrival rate (measured by exponential weighted moving average) in
packets per second.

=h CLICK_LLRPC_GET_COUNT llrpc

Argument is a pointer to an integer that must be 0 (packet count) or 1 (byte
count). Returns the current packet or byte count.

=h CLICK_LLRPC_GET_COUNTS llrpc

Argument is a pointer to a click_llrpc_counts_st structure (see
<click/llrpc.h>). The C<keys> components must be 0 (packet count) or 1 (byte
count). Stores the corresponding counts in the corresponding C<values>
components.

*/

/**
 * Ancestor of all types of counter so external elements can access count and byte_count
 * independently of the storage type
 */
class CounterT { public :
    virtual counter_int_type count() = 0;
    virtual counter_int_type byte_count() = 0;
};

template <typename counter_t>
class CounterBase : public Element, public CounterT { public:

	CounterBase() CLICK_COLD;
    ~CounterBase() CLICK_COLD;

    void* cast(const char *name);

    void reset();

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;
    int llrpc(unsigned, void *);

  protected:

#ifdef HAVE_INT64_TYPES
    // Reduce bits of fraction for byte rate to avoid overflow
    typedef RateEWMAX<RateEWMAXParameters<4, 10, uint64_t, int64_t> > rate_t;
    typedef RateEWMAX<RateEWMAXParameters<4, 4, uint64_t, int64_t> > byte_rate_t;
#else
    typedef RateEWMAX<RateEWMAXParameters<4, 10> > rate_t;
    typedef RateEWMAX<RateEWMAXParameters<4, 4> > byte_rate_t;
#endif

    counter_t _count;
    counter_t _byte_count;
    rate_t _rate;
    byte_rate_t _byte_rate;

    counter_int_type _count_trigger;
    HandlerCall *_count_trigger_h;

    counter_int_type _byte_trigger;
    HandlerCall *_byte_trigger_h;

    bool _count_triggered : 1;
    bool _byte_triggered : 1;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;

};

class Counter : public CounterBase<counter_int_type> { public:

	Counter() CLICK_COLD;
    ~Counter() CLICK_COLD;

    const char *class_name() const		{ return "Counter"; }
    const char *processing() const		{ return AGNOSTIC; }
    const char *port_count() const		{ return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("Counter", name) == 0)
            return (Counter *)this;
        else
            return CounterBase::cast(name);
    }

    Packet *simple_action(Packet *);

    counter_int_type count() {
        return counter_sum(_count);
    }

    counter_int_type byte_count() {
        return counter_sum(_byte_count);
    }
};


class CounterMP : public CounterBase<per_thread<counter_int_type> > { public:

	CounterMP() CLICK_COLD;
    ~CounterMP() CLICK_COLD;

    const char *class_name() const		{ return "CounterMP"; }
    const char *processing() const		{ return AGNOSTIC; }
    const char *port_count() const		{ return PORTS_1_1; }

    void* cast(const char *name)
    {
        if (strcmp("CounterMP", name) == 0)
            return (CounterMP *)this;
        else
            return CounterBase::cast(name);
    }

    Packet *simple_action(Packet *);

    counter_int_type count() {
        return counter_sum(_count);
    }

    counter_int_type byte_count() {
        return counter_sum(_byte_count);
    }
};

CLICK_ENDDECLS
#endif
