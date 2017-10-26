/*
 * MidStat.{cc,hh} -- remove insults in web pages
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "midstat.hh"
#include "tcpelement.hh"

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




void MidStat::push_batch(int port, fcb_MidStat* MidStat, PacketBatch* flow)
{


}


CLICK_ENDDECLS
EXPORT_ELEMENT(MidStat)
ELEMENT_MT_SAFE(MidStat)
