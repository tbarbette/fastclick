// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_FLOWSTRIP_HH
#define CLICK_FLOWSTRIP_HH
#include <click/batchelement.hh>
#include <click/flowelement.hh>
CLICK_DECLS

/*
 * =c
 * FlowStrip(LENGTH)
 * =s basicmod
 * FlowStrips bytes from front of packets
 * =d
 * Deletes the first LENGTH bytes from each packet.
 * =e
 * Use this to get rid of the Ethernet header:
 *
 *   FlowStrip(14)
 * =a FlowStripToNetworkHeader, FlowStripIPHeader, EtherEncap, IPEncap, Truncate
 */

class FlowStrip : public FlowElement { public:

    FlowStrip() CLICK_COLD;

    const char *class_name() const		{ return "FlowStrip"; }
    const char *port_count() const		{ return PORTS_1_1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    PacketBatch *simple_action_batch(PacketBatch *);

    void apply_offset(FlowNode* node) {
    	node->level()->add_offset(_nbytes);

    	FlowNode::NodeIterator* it = node->iterator();
		FlowNodePtr* child;
		while ((child = it->next()) != 0) {
			if (child->ptr && child->is_node())
				apply_offset(child->node);
		}
		if (node->default_ptr()->ptr && node->default_ptr()->is_node())
			apply_offset(node->default_ptr()->node);
    }

    FlowNode* get_table() {
    	FlowNode* root = FlowElementVisitor::get_downward_table(this, 0);
    	apply_offset(root);
    	return root;
    }
  private:

    unsigned _nbytes;

};

CLICK_ENDDECLS
#endif
