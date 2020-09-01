/*
 * flowcounter.{cc,hh} -- remove insults in web pages
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "flowcounter.hh"

CLICK_DECLS

CTXCounter::CTXCounter()
{

}

int CTXCounter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .complete() < 0)
        return -1;

    return 0;
}

void CTXCounter::push_flow(int, int* fcb, PacketBatch* flow)
{
    *fcb += flow->length();
    output_push_batch(0, flow);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(CTXCounter)
ELEMENT_MT_SAFE(CTXCounter)
