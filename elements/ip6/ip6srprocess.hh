#ifndef CLICK_IP6SRProcess_HH
#define CLICK_IP6SRProcess_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

CLICK_DECLS

/*
=c

IP6SRProcess(ADDR[, ADDR, ...])

=s ip

Processes the Segment Routing Header of the IPv6 packet

=d

Takes a list of adresses: SIDs triggering SRv6 functions. 

=e


  IP6SRProcess(2000:10:1::2, 2000:20:1::3, ...)

=a IP6SRProcess */

class IP6SRProcess : public SimpleElement<IP6SRProcess> { public:

  IP6SRProcess();
  ~IP6SRProcess();

  const char *class_name() const override        { return "IP6SRProcess"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }

  Packet *simple_action(Packet *);
};

CLICK_ENDDECLS
#endif
