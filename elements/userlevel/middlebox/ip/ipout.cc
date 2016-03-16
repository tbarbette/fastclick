#include <click/config.h>
#include "ipout.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>

CLICK_DECLS

IPOut::IPOut()
{

}

int IPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* IPOut::processPacket(Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    // Recompute the IP checksum if the packet has been modified
    if(getAnnotationModification(packet))
        computeChecksum(packet);

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPOut)
//ELEMENT_MT_SAFE(IPOut)
