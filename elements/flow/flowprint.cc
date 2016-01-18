/*
 * flowprint.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include <click/flowelement.hh>
#include "flowprint.hh"

CLICK_DECLS

int
FlowPrint::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
  	 .complete() < 0)
    	return -1;

    return 0;
}


int FlowPrint::initialize(ErrorHandler *errh) {


	return 0;
}



void FlowPrint::push_batch(int port, PacketBatch* batch) {
	if (!fcb_stack){
		click_chatter("%d packets not from a specific flow...",batch->count());
		return;
	}
	click_chatter("%d packets from flow %lu. Count : %d.",batch->count(),fcb_stack->node_data[0], fcb_stack->count());
	output_push_batch(0,batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowPrint)
