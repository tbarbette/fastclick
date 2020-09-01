/*
 * udpin.{cc,hh} -- entry point of an IP path in the stack of the middlebox
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "udpout.hh"

CLICK_DECLS

UDPOut::UDPOut()
{

}

int UDPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    errh->warning("UDPOut is useless, you can remove it.");
    return 0;
}

void UDPOut::push_batch(int port, PacketBatch* flow)
{
    output(0).push_batch(flow);
}


CLICK_ENDDECLS
EXPORT_ELEMENT(UDPOut)
ELEMENT_MT_SAFE(UDPOut)
