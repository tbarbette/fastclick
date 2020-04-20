#ifndef RTCYCLES_HH
#define RTCYCLES_HH
#include <click/batchelement.hh>

/*
 * =c
 * RoundTripCycleCount()
 *
 * =s counters
 * measures round trip cycles on a push or pull path
 *
 * =d
 *
 * Measures the number of CPU cycles it takes for a push or pull to come back
 * to the element. This is a good indication of how much CPU is spent on the
 * Click path after or before this element.
 *
 * =h packets read-only
 * Returns the number of packets that have passed.
 *
 * =h cycles read-only
 * Returns the accumulated round-trip cycles for all passing packets.
 *
 * =h reset_counts write-only
 * Resets C<packets> and C<cycles> counters to zero when written.
 *
 * =a SetCycleCount, CycleCountAccum, SetPerfCount, PerfCountAccum
 */

class RTCycles : public BatchElement { public:

  RTCycles() CLICK_COLD;
  ~RTCycles() CLICK_COLD;

  const char *class_name() const	{ return "RoundTripCycleCount"; }
  const char *port_count() const	{ return PORTS_1_1; }

  void push(int, Packet *p);
  Packet *pull(int);
#if HAVE_BATCH
  void push_batch(int, PacketBatch *b) override;
  PacketBatch *pull_batch(int,unsigned) override;
#endif
  void add_handlers() CLICK_COLD;

  struct state {
	  state() : accum(0), npackets(0) {

	  }
    click_cycles_t accum;
    click_cycles_t npackets;
  };
  per_thread<state> _state;


  static String read_handler(Element *, void *) CLICK_COLD;
  static int reset_handler(const String &, Element*, void*, ErrorHandler*);

};

#endif
