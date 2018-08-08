/*
 * flowasciiprint.{cc,hh}
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "flowasciiprint.hh"
#include "tcpelement.hh"

CLICK_DECLS

FlowASCIIPrint::FlowASCIIPrint() : _active(true)
{

}

int FlowASCIIPrint::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
        .read("ACTIVE", _active)
            .complete() < 0)
        return -1;

    return 0;
}

void FlowASCIIPrint::push_batch(int port, fcb_FlowASCIIPrint* fcb, PacketBatch* flow)
{
    if (!_active) {
        output_push_batch(0, flow);
        return;
    }

    fcb->flowBuffer.enqueueAll(flow);
    auto iter = fcb->flowBuffer.contentBegin();
    if (!iter.current()) {
        goto finished;
    }
    click_chatter("Content of the stream:");
    iter.print_ascii();

    finished:

    output_push_batch(0, fcb->flowBuffer.dequeueAll());
}


CLICK_ENDDECLS
EXPORT_ELEMENT(FlowASCIIPrint)
ELEMENT_MT_SAFE(FlowASCIIPrint)
