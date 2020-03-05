#ifndef SETCYCLECOUNT_HH
#define SETCYCLECOUNT_HH

/*
 * =c
 * SetCycleCount()
 * =s counters
 * stores cycle count in annotation
 * =d
 *
 * Stores the current cycle count in an annotation in each packet. In
 * combination with CycleCountAccum, this lets you measure how many cycles it
 * takes a packet to pass from one point to another.
 *
 * =n
 *
 * A packet has room for either exactly one cycle count or exactly one
 * performance metric.
 *
 * =a CycleCountAccum, RoundTripCycleCount, SetPerfCount, PerfCountAccum */

#include <click/batchelement.hh>

class SetCycleCount : public SimpleElement<SetCycleCount> { public:

  SetCycleCount() CLICK_COLD;
  ~SetCycleCount() CLICK_COLD;

  const char *class_name() const		{ return "SetCycleCount"; }
  const char *port_count() const		{ return PORTS_1_1; }

  Packet* simple_action(Packet *p);
};

#endif
