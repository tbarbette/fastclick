#ifndef CLICK_RRSWITCH_HH
#define CLICK_RRSWITCH_HH
#include <click/batchelement.hh>
#include <click/atomic.hh>
CLICK_DECLS

/*
 * =c
 * RoundRobinSwitch
 * =s classification
 * sends packets to round-robin outputs
 * =io
 * one input, one or more outputs
 * =d
 * Pushes each arriving packet to one of the N outputs. The next packet
 * will be pushed to the following output in round-robin order.
 *
 * =a StrideSwitch, Switch, HashSwitch, RandomSwitch, RoundRobinSched
 */

class RoundRobinSwitch : public BatchElement {

  atomic_uint32_t _next;
  uint32_t _max;
  bool _split_batch;
 public:

  RoundRobinSwitch() CLICK_COLD;

  const char *class_name() const override	{ return "RoundRobinSwitch"; }
  const char *port_count() const override	{ return "1/1-"; }
  const char *processing() const override	{ return PUSH; }

  int configure(Vector<String> &conf, ErrorHandler *errh);

  void push(int, Packet *);
#if HAVE_BATCH
  void push_batch(int, PacketBatch *);
#endif
  void add_handlers();

 private:
  inline int next(Packet*);
};

CLICK_ENDDECLS
#endif
