/*
 * flowpaint.{cc,hh} -- element sets packets' FlowPaint annotation
 */



#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include "flowpaint.hh"
CLICK_DECLS

FlowPaint::FlowPaint()
{
}

int
FlowPaint::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	.read_mp("COLOR", _color)
	.complete() < 0)
	return -1;
    return 0;
}

void
FlowPaint::push_flow(int port, int* flowdata, PacketBatch* head)
{
	*flowdata = _color;
	//click_chatter("%s : %d packets colored with %d",name().c_str(),head->count(),_color);
	output_push_batch(0,head);
}

void
FlowPaint::add_handlers()
{
    add_data_handlers("color", Handler::OP_READ | Handler::OP_WRITE, &_color);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(FlowPaint)
ELEMENT_MT_SAFE(FlowPaint)
