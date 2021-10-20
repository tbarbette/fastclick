#ifndef CLICK_LOOKUPIP6ROUTE_HH
#define CLICK_LOOKUPIP6ROUTE_HH
#include <click/batchelement.hh>
#include <click/ip6table.hh>
#include "ip6routetable.hh"
CLICK_DECLS

/*
 * =c
 * LookupIP6Route(DST1 MASK1 GW1 OUT1, DST2 MAS2 GW2 OUT2, ...)
 * =s ip6
 *
 * =d
 * Input: IP6 packets (no ether header).
 * Expects a destination IP6 address annotation with each packet.
 * Looks up the address, sets the destination annotation to
 * the corresponding GW (if non-zero), and emits the packet
 * on the indicated OUTput.
 *
 * Each comma-separated argument is a route, specifying
 * a destination and mask, a gateway (zero means none),
 * and an output index.
 *
 * =e
 *
 *   ... -> GetIP6Address(24) -> rt;
 *   rt :: LookupIP6Route(
 *          3ffe:1ce1:2::/128 ::0 0,
 *          3ffe:1ce1:2:0:200::/128  ::0 0,
 *          3ffe:1ce1:2:/80 ::0 1,
 *          3ffe:1ce1:2:0:200::/80: ::0 2,
 *          0::ffff:0:0/96 ::0 3,
 *          ::0/0 3ffe:1ce1:2::2 1);

 *   rt[0] -> ToLinux;
 *   rt[1] -> ... -> ToDevice(eth0);
 *   rt[2] -> ... -> ToDevice(eth1);
 *   ...
 *
 */

class LookupIP6Route : public ClassifyElement<LookupIP6Route,IP6RouteTable> {
public:
  LookupIP6Route();
  ~LookupIP6Route();

  const char *class_name() const override		{ return "LookupIP6Route"; }
  const char *port_count() const override		{ return "1/-"; }
  const char *processing() const override		{ return PUSH; }

  int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
  int initialize(ErrorHandler *) override CLICK_COLD;
  void add_handlers() override CLICK_COLD;

  int classify(Packet *p);

  int add_route(IP6Address, IP6Address, IP6Address, int, ErrorHandler *);
  int remove_route(IP6Address, IP6Address, ErrorHandler *);
  String dump_routes()				{ return _t.dump(); };

private:

  IP6Table _t;

  IP6Address _last_addr;
  IP6Address _last_gw;
  int _last_output;

#ifdef IP_RT_CACHE2
  IPAddress _last_addr2;
  IPAddress _last_gw2;
  int _last_output2;
#endif

};

CLICK_ENDDECLS
#endif
