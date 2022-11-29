#ifndef CTXIPRoute_HH
#define CTXIPRoute_HH
#include <click/batchelement.hh>
#include <click/iptable.hh>
#include "ctxdispatcher.hh"
CLICK_DECLS

/*
 * =c
 * CTXIPRoute(DST1/MASK1 [GW1] OUT1, DST2/MASK2 [GW2] OUT2, ...)
 *
 * =s ctx
 *
 * IP routing table using the flow system
 * V<classification>
 *
 * =d
 * Interfaces are exactly the same as LookupIPRoute. But it is using the ctx
 * system to do the lookup in advance.
 *
 * =a LookupIPRoute
 */

class CTXIPRoute : public CTXDispatcher {


  Vector<IPAddress> _gw;

public:
  CTXIPRoute() CLICK_COLD;
  ~CTXIPRoute() CLICK_COLD;

  const char *class_name() const		{ return "CTXIPRoute"; }
  const char *port_count() const		{ return "1/-"; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  void push_flow(int, int* flowdata, PacketBatch* batch) override;
};

CLICK_ENDDECLS
#endif
