#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include "ipout.hh"

CLICK_DECLS

IPOut::IPOut()
{
    counter = 0;
}

int IPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* IPOut::processPacket(struct fcb*, Packet* p)
{
    int flowDirection = determineFlowDirection();

    click_chatter("Flow %d was managed by thread %u", flowDirection, click_current_cpu_id());

    WritablePacket *packet = p->uniqueify();

    /*
    // Test to check retransmission
    counter++;
    if(counter == 5 && flowDirection == 1)
    {
        packet->kill();
        return NULL;
    }
    */
    // Recompute the IP checksum if the packet has been modified
    if(getAnnotationDirty(packet))
        computeIPChecksum(packet);

    return packet;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPOut)
ELEMENT_REQUIRES(IPElement)
ELEMENT_MT_SAFE(IPOut)
