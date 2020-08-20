/*
 * TestFlowSpace.{cc,hh}
 */

#include "testflowspace.hh"

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/flow/flow.hh>

CLICK_DECLS

TestFlowSpace::TestFlowSpace() {

};

TestFlowSpace::~TestFlowSpace() {

}

int
TestFlowSpace::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
               .complete() < 0)
        return -1;

    return 0;
}


int TestFlowSpace::initialize(ErrorHandler *errh) {
    return 0;
}

void TestFlowSpace::push_flow(int port, FourBytes* flowdata, PacketBatch* batch) {
    output_push_batch(0, batch);
}


CLICK_ENDDECLS

ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(TestFlowSpace)
ELEMENT_MT_SAFE(TestFlowSpace)
