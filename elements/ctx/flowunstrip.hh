#ifndef CLICK_FLOWUNSTRIP_HH
#define CLICK_FLOWUNSTRIP_HH
#include <click/batchelement.hh>
#include <click/flow/flow.hh>
#include <click/flow/flowelement.hh>
CLICK_DECLS

/*
 * =c
 * FlowUnstrip(LENGTH)
 * =s basicmod
 * FlowUnstrips bytes from front of packets
 * =d
 * Put LENGTH bytes at the front of the packet. These LENGTH bytes may be bytes
 * previously removed by Strip.
 * =e
 * Use this to get rid of the Ethernet header and put it back on:
 *
 *   FlowStrip(14) -> ... -> FlowUnstrip(14)
 * =a EtherEncap, IPEncap
 */

class FlowUnstrip : public FlowElement {

  unsigned _nbytes;

 public:

  FlowUnstrip(unsigned nbytes = 0);

  const char *class_name() const	{ return "FlowUnstrip"; }
  const char *port_count() const	{ return PORTS_1_1; }

  int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

  PacketBatch *simple_action_batch(PacketBatch *);

  void apply_offset(FlowNode* node, bool invert);
  FlowNode* get_table(int,Vector<FlowElement*> context) override;
  FlowNode* resolveContext(FlowType t, Vector<FlowElement*> contextStack) override;
};


CLICK_ENDDECLS
#endif
