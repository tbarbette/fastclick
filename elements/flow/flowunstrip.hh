#ifndef CLICK_FLOWUNSTRIP_HH
#define CLICK_FLOWUNSTRIP_HH
#include <click/batchelement.hh>
#include <click/flowelement.hh>
#include <click/flow.hh>
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

  inline void apply_offset(FlowNode* node) {
	node->level()->add_offset(-_nbytes);

  	FlowNode::NodeIterator* it = node->iterator();
		FlowNodePtr* child;
		while ((child = it->next()) != 0) {
			if (child->ptr && child->is_node())
				apply_offset(child->node);
		}
		if (node->default_ptr()->ptr && node->default_ptr()->is_node())
			apply_offset(node->default_ptr()->node);
  }

  FlowNode* get_table(int) override;

};

CLICK_ENDDECLS
#endif
