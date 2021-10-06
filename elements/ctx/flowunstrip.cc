/*
 * FlowUnstrip.{cc,hh} -- element FlowUnstrips bytes from front of packet
 * Robert Morris, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */
#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packetbatch.hh>
#include "flowunstrip.hh"
CLICK_DECLS

FlowUnstrip::FlowUnstrip(unsigned nbytes)
  : _nbytes(nbytes)
{
	in_batch_mode = BATCH_MODE_NEEDED;
}

int
FlowUnstrip::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return Args(conf, this, errh).read_mp("LENGTH", _nbytes).complete();
}
PacketBatch *
FlowUnstrip::simple_action_batch(PacketBatch *head)
{
	EXECUTE_FOR_EACH_PACKET_DROPPABLE([this](Packet* p){return p->push(_nbytes);},head,[](Packet* p){})
	return head;
}


inline void FlowUnstrip::apply_offset(FlowNode* node, bool invert) {
  node->level()->add_offset(invert?_nbytes:-_nbytes);

  FlowNode::NodeIterator it = node->iterator();
      FlowNodePtr* child;
      while ((child = it.next()) != 0) {
          if (child->ptr && child->is_node())
              apply_offset(child->node,invert);
      }
      if (node->default_ptr()->ptr && node->default_ptr()->is_node())
          apply_offset(node->default_ptr()->node,invert);
}


FlowNode*
FlowUnstrip::get_table(int,Vector<FlowElement*> context) {
    context.push_back(this);
	FlowNode* root = FlowElementVisitor::get_downward_table(this, 0, context);
	if (root)
		apply_offset(root, false);
	return root;
 }

FlowNode* FlowUnstrip::resolveContext(FlowType t, Vector<FlowElement*> contextStack) {
    if (contextStack.size() > 1) {
        FlowNode* n = contextStack[contextStack.size() - 2]->resolveContext(t, contextStack.sub(0,contextStack.size()-1));
        if (n) {
            apply_offset(n, true);
            return n;
        }
    }
    return FlowElement::resolveContext(t,contextStack);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowUnstrip)
ELEMENT_MT_SAFE(FlowUnstrip)
