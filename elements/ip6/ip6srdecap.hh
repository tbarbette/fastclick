#ifndef CLICK_IP6SRDecap_HH
#define CLICK_IP6SRDecap_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

CLICK_DECLS

/*
=c

IP6SRDecap(ADDR[, ADDR, ...])

=s ip

adds a SR Header to the IP6 packet

=d

Takes a list of adresses

=e


  IP6SRDecap(2000:10:1::2, 2000:20:1::3, ...)

=a IP6Encap */

class IP6SRDecap : public SimpleElement<IP6SRDecap> { public:

  IP6SRDecap();
  ~IP6SRDecap();

  const char *class_name() const override        { return "IP6SRDecap"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }

  Packet *simple_action(Packet *);

};

CLICK_ENDDECLS
#endif
