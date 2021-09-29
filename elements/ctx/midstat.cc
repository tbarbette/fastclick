/*
 * midstat.{cc,hh} -- MiddleBox statistics
 * Tom Barbette
 *
 * Based on the idea from mOS
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "midstat.hh"

CLICK_DECLS

MidStat::MidStat()
{

}

int MidStat::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .complete() < 0)
        return -1;



    return 0;
}


void MidStat::push_flow(int port, fcb_MidStat* fcb, PacketBatch* flow)
{
    fcb->count += flow->count();
    output(0).push_batch(flow);

}


CLICK_ENDDECLS
EXPORT_ELEMENT(MidStat)
ELEMENT_MT_SAFE(MidStat)
