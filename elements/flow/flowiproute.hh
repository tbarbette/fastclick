#ifndef FLOWIPROUTE_HH
#define FLOWIPROUTE_HH
#include <click/batchelement.hh>
#include <click/iptable.hh>
#include "flowdispatcher.hh"
CLICK_DECLS

/*
 * =c
 * FlowIPRoute(DST1/MASK1 [GW1] OUT1, DST2/MASK2 [GW2] OUT2, ...)
 * =s threads
 * IP routing table using the flow system
 * V<classification>
 * =d
 * Interfaces are exactly the same as LookupIPRoute. But it is using the flow
 * system to do the lookup in advance.
 *
 * =a LookupIPRoute
 */

class FlowIPRoute : public FlowDispatcher {


  Vector<IPAddress> _gw;

public:
  FlowIPRoute() CLICK_COLD;
  ~FlowIPRoute() CLICK_COLD;

  const char *class_name() const		{ return "FlowIPRoute"; }
  const char *port_count() const		{ return "1/-"; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;

  void push_flow(int, int* flowdata, PacketBatch* batch) override;
};

CLICK_ENDDECLS
#endif
