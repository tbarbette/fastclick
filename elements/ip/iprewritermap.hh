#ifndef CLICK_IPREWRITERMAP_HH
#define CLICK_IPREWRITERMAP_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/hashmap.hh>
#include <click/sync.hh>
CLICK_DECLS

/*
=c

IPRewriterMap(BEGIN, END)

=s ip

element maps and rewrite IPs to a subnet

=d

Expects IP packets as input.  Should be placed downstream of a
CheckIPHeader or equivalent element.
*/

class IPRewriterMap : public Element { public:

  IPRewriterMap() CLICK_COLD;
  ~IPRewriterMap() CLICK_COLD;

  const char *class_name() const		{ return "IPRewriterMap"; }
  const char *port_count() const		{ return "2/2"; }
  const char *processing() const		{ return PUSH; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
  int initialize(ErrorHandler *) CLICK_COLD;
  void cleanup(CleanupStage) CLICK_COLD;
  void add_handlers() CLICK_COLD;

  void push(int,Packet *);

private:
  IPAddress _begin;
  IPAddress _end;
  HashMap<IPAddress, int > _map;
  Vector<IPAddress> _dest;
  atomic_uint32_t _current_dest;
  Spinlock _set_lock;
};

CLICK_ENDDECLS
#endif
