/*
 * flowprint.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include "flowprint.hh"
#include <click/flow/flowelement.hh>

CLICK_DECLS

int
FlowPrint::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .read_or_set_p("CONTINUE", _continue, false)
            .read_or_set("PTR", _show_ptr, false)
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
		if (_continue)
	        output_push_batch(0,batch);

        return;
	}
    if (_show_ptr)
	click_chatter("%p{element}: %d packets from flow %lu. Count : %d. Ptr %p", this, batch->count(),fcb_stack->node_data[0], fcb_stack->count(), fcb_stack);
    else
	click_chatter("%p{element}: %d packets from flow %lu. Count : %d", this, batch->count(),fcb_stack->node_data[0], fcb_stack->count());
	output_push_batch(0,batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowPrint)
