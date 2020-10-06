#ifndef LOOKUPIPROUTEMP_HH
#define LOOKUPIPROUTEMP_HH
#include <click/batchelement.hh>
#include <click/iptable.hh>
CLICK_DECLS

/*
 * =c
 * LookupIPRouteMP(DST1/MASK1 [GW1] OUT1, DST2/MASK2 [GW2] OUT2, ...)
 * =s threads
 * simple static IP routing table
 * V<classification>
 * =d
 * Interfaces are exactly the same as LookupIPRoute. The only difference is
 * when operating in SMP mode, each processor has a processor local cache,
 * hence the element is MT SAFE.
 *
 * =a LookupIPRoute
 */

class LookupIPRouteMP : public BatchElement {
  struct cache_entry {
    IPAddress _last_addr_1;
    IPAddress _last_gw_1;
    int _last_output_1;
    IPAddress _last_addr_2;
    IPAddress _last_gw_2;
    int _last_output_2;
  } __attribute__((aligned(64)));

#if CLICK_USERLEVEL && HAVE_MULTITHREAD
  struct cache_entry *_cache;
#else
  struct cache_entry _cache[CLICK_CPU_MAX];
#endif

  IPTable _t;

public:
  LookupIPRouteMP() CLICK_COLD;
  ~LookupIPRouteMP() CLICK_COLD;

  const char *class_name() const override		{ return "LookupIPRouteMP"; }
  const char *port_count() const override		{ return "1/-"; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage stage) CLICK_COLD;

#if HAVE_BATCH
  void push_batch(int port, PacketBatch *p);
#endif
  void push(int port, Packet *p);
};

CLICK_ENDDECLS
#endif
