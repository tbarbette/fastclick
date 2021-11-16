/*
 * flowrrswitch.{cc,hh}
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/routervisitor.hh>
#include "flowrrswitch.hh"
#include <click/flow/flowelement.hh>

CLICK_DECLS


FlowRoundRobinSwitch::FlowRoundRobinSwitch() : _rr(0) {
}

int
FlowRoundRobinSwitch::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
            .complete() < 0)
	return -1;

    return 0;
}


int FlowRoundRobinSwitch::initialize(ErrorHandler *errh) {

	return 0;
}



void FlowRoundRobinSwitch::push_flow(int port, int* rr, PacketBatch* batch) {
    if (*rr == 0) {
        *rr = (((*_rr)++) % noutputs()) + 1;
    }

	output_push_batch(*rr - 1, batch);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(flow)
EXPORT_ELEMENT(FlowRoundRobinSwitch)
