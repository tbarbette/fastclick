#ifndef CLICK_AVERAGECOUNTER_HH
#define CLICK_AVERAGECOUNTER_HH
#include <click/batchelement.hh>
#include <click/ewma.hh>
#include <click/atomic.hh>
#include <click/timer.hh>
CLICK_DECLS

/*
 * =c
 * AverageCounter([IGNORE])
 * =s counters
 * measures historical packet count and rate
 * =d
 *
 * Passes packets unchanged from its input to its
 * output, maintaining statistics information about
 * packet count and packet rate using a strict average.
 *
 * The rate covers only the time between the first and
 * most recent packets.
 *
 * IGNORE, by default, is 0. If it is greater than 0,
 * the first IGNORE number of seconds are ignored in
 * the count.
 *
 * =h count read-only
 * Returns the number of packets that have passed through since the last reset.
 *
 * =h byte_count read-only
 * Returns the number of packets that have passed through since the last reset.
 *
 * =h rate read-only
 * Returns packet arrival rate.
 *
 * =h byte_rate read-only
 * Returns packet arrival rate in bytes per second.  (Beware overflow!)
 *
 * =h reset write-only
 * Resets the count and rate to zero.
 */

class AverageCounter : public BatchElement { public:

    AverageCounter() CLICK_COLD;

    const char *class_name() const		{ return "AverageCounter"; }
    const char *port_count() const		{ return PORTS_1_1; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    uint64_t count() const			{ return _count; }
    uint64_t byte_count() const			{ return _byte_count; }
    uint64_t first() const			{ return _first; }
    uint64_t last() const			{ return _last; }
    uint64_t ignore() const			{ return _ignore; }
    void reset();

    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

#if HAVE_BATCH
    PacketBatch *simple_action_batch(PacketBatch *batch);
#endif
    Packet *simple_action(Packet *);

  private:

    volatile uint64_t _count;
    volatile uint64_t _byte_count;
    volatile uint64_t _first;
    volatile uint64_t _last;
    uint64_t _ignore;

};

CLICK_ENDDECLS
#endif
