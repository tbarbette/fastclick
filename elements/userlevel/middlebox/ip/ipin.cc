#include <click/config.h>
#include "ipin.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>

CLICK_DECLS

IPIn::IPIn()
{

}

int IPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* IPIn::processPacket(Packet* p)
{
    setAnnotationModification(p, false);

    // Compute the offset of the IP payload
    const click_ip *iph = p->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint32_t offset = (int)(p->network_header() + iph_len - p->data());
    setContentOffset(p, offset);

    return p;
}

void IPIn::packetModified(Packet* p, int)
{
    // Annotate the packet to indicate it has been modified
    // While going through "out elements", the checksum will be recomputed
    setAnnotationModification(p, true);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPIn)
//ELEMENT_MT_SAFE(IPIn)
