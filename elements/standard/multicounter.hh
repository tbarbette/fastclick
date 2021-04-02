// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_MULTICOUNTER_HH
#define CLICK_MULTICOUNTER_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
CLICK_DECLS

/*
=c

MultiCounter()

=s counters

measures packet count and rate for multiple inputs

=d

Passes packets unchanged from its input to its output, maintaining statistics
information about packet count and packet rate.


=h count read-only

Returns the number of packets that have passed through since the last reset.
It's returned as a list of values seperated with ','. Value I<i> in the list
corrseponds to input I<i>

=h byte_count read-only

Returns the number of bytes that have passed through since the last reset.
It's returned as a list of values seperated with ','. Value I<i> in the list
corrseponds to input I<i>

=h rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in packets per second.
It's returned as a list of values seperated with ','. Value I<i> in the list
corrseponds to input I<i>

=h byte_rate read-only

Returns the recent arrival rate, measured by exponential
weighted moving average, in bytes per second.
It's returned as a list of values seperated with ','. Value I<i> in the list
corrseponds to input I<i>

=h reset_counts write-only

Resets the counts and rates to zero.

*/

class MultiCounter : public BatchElement {
	public:

		MultiCounter() CLICK_COLD;
		~MultiCounter() CLICK_COLD;

		const char *class_name() const		{ return "MultiCounter"; }
		const char *port_count() const        { return "1-/="; }
		const char *processing() const    { return PUSH; }
		const char *flow_code() const         { return "#/#"; }

		void reset();
		int initialize(ErrorHandler *) CLICK_COLD;
		void cleanup(CleanupStage stage) CLICK_COLD;
		void add_handlers() CLICK_COLD;

		void push      (int, Packet      *);
	#if HAVE_BATCH
		void push_batch(int, PacketBatch *);
	#endif

	private:
		void update(Packet *p, int port);

		#ifdef HAVE_INT64_TYPES
		typedef uint64_t counter_t;
		// Reduce bits of fraction for byte rate to avoid overflow
		typedef RateEWMAX<RateEWMAXParameters<4, 10, uint64_t, int64_t> > rate_t;
		typedef RateEWMAX<RateEWMAXParameters<4, 4, uint64_t, int64_t> > byte_rate_t;
		#else
		typedef uint32_t counter_t;
		typedef RateEWMAX<RateEWMAXParameters<4, 10> > rate_t;
		typedef RateEWMAX<RateEWMAXParameters<4, 4> > byte_rate_t;
		#endif

		counter_t *_count;
		counter_t *_byte_count;
		rate_t *_rate;
		byte_rate_t *_byte_rate;

		static String format_counts(counter_t * counts, int size);
		static String format_rates(rate_t *rates, int size);
		static String format_byte_rates(byte_rate_t *byte_rates, int size);
		static String read_handler(Element *, void *) CLICK_COLD;
		static int write_handler(const String&, Element*, void*, ErrorHandler*) CLICK_COLD;

};

CLICK_ENDDECLS
#endif
