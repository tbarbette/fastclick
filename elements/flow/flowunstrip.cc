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
	EXECUTE_FOR_EACH_PACKET([this](Packet* p){return p->push(_nbytes);},head)
			/*
	Packet* current = head;
	Packet* last = NULL;
	while (current != NULL) {
	    Packet* pkt = current->push(_nbytes);
	    if (pkt != current) {
	        click_chatter("Warning : had not enough bytes to FlowUnstrip. Allocate more headroom !");
            if (last) {
                last->set_next(pkt);
            } else {
                head= static_cast<PacketBatch*>(pkt);
            }
            current = pkt;
        }
        last = current;

		current = current->next();
	}*/
	return head;
}


FlowNode*
FlowUnstrip::get_table(int) {
 	FlowNode* root = FlowElementVisitor::get_downward_table(this, 0);
 	if (root)
 		apply_offset(root);
 	return root;
 }
CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowUnstrip)
ELEMENT_MT_SAFE(FlowUnstrip)
