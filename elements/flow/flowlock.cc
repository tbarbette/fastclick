/*
 * FlowLock.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>
#include "flowlock.hh"

CLICK_DECLS

FlowLock::FlowLock() {

};

FlowLock::~FlowLock() {

}

int
FlowLock::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .complete() < 0)
        return -1;

    return 0;
}


int FlowLock::initialize(ErrorHandler *errh) {
    return 0;
}

void FlowLock::push_batch(int port, FlowLockState* flowdata, PacketBatch* batch) {
	flowdata->lock.acquire();
    output_push_batch(0, batch);
	flowdata->lock.release();
}


CLICK_ENDDECLS

EXPORT_ELEMENT(FlowLock)
ELEMENT_MT_SAFE(FlowLock)
