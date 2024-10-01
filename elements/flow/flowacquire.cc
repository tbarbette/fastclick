/*
 * FlowAcquire.{cc,hh} -- acquire refernece to flow
 */



#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/packet_anno.hh>
#include "flowacquire.hh"
CLICK_DECLS

FlowAcquire::FlowAcquire()
{
}

int
FlowAcquire::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
	//.read_mp("BATCH", _batch)
	.complete() < 0)
	return -1;
    return 0;
}

void
FlowAcquire::push_flow(int port, bool* seen, PacketBatch* head)
{
	if (!*seen) {
		fcb_acquire(1);
		*seen = true;
	}
	output_push_batch(0,head);
}

void
FlowAcquire::add_handlers()
{
}

CLICK_ENDDECLS
EXPORT_ELEMENT(FlowAcquire)
ELEMENT_MT_SAFE(FlowAcquire)
