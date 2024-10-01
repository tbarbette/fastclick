#ifndef CLICK_IP6SRENCAP_HH
#define CLICK_IP6SRENCAP_HH
#include <click/batchelement.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

CLICK_DECLS

/*
=c

IP6SREncap(ADDR[, ADDR, ...])

=s ip

adds a SR Header to the IP6 packet

=d

Takes a list of adresses

=e


  IP6SREncap(2000:10:1::2, 2000:20:1::3, ...)

=a IP6Encap */

class IP6SREncap : public SimpleElement<IP6SREncap> { public:

  IP6SREncap();
  ~IP6SREncap();

  const char *class_name() const override        { return "IP6SREncap"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }

  Packet *simple_action(Packet *);

  int _sr_len;
  click_ip6_sr* _sr;
  bool _do_encap_dst;
};

CLICK_ENDDECLS
#endif
