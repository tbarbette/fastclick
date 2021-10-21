#ifndef CLICK_IP6_DROP_HH
#define CLICK_IP6_DROP_HH
#include <click/element.hh>
#include <click/glue.hh>
#include <click/atomic.hh>
#include <clicknet/ip6.h>
#include <click/ip6address.hh>

CLICK_DECLS

/*
=c

IP6Drop(ADDR[, ADDR, ...], P, R, K, H)

=s ip

Gilbert-Elliott drop model

=d

=e


  IP6SRv6FECDecode(fc00::a, fc00::9)

=a IP6Encap */

enum state_e {
  good,
  bad
};

class IP6Drop : public Element { 
 
 public:

  IP6Drop();
  ~IP6Drop();

  const char *class_name() const override        { return "IP6Drop"; }
  const char *port_count() const override        { return PORTS_1_1; }

  int  configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  bool can_live_reconfigure() const     { return true; }
  void add_handlers() CLICK_COLD;
  String read_handler(Element *e, void *thunk) CLICK_COLD;

  Packet *simple_action(Packet *p_in);
  bool gemodel() CLICK_COLD;
  bool addr_eq(uint32_t *a1, uint32_t *a2) CLICK_COLD;

 private:
  Vector<IP6Address> addrs;
  uint16_t total_seen; // Number of analyzed packets
  double p; // Good -> bad
  double r; // Bad -> good
  double h; // Don't drop in bad
  double k; // Don't drop in good
  state_e state; // State of the machine
  uint16_t seed;
  bool is_in_good;
  bool _use_dst_anno;
};

CLICK_ENDDECLS
#endif

