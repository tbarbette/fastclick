#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include "ipin.hh"

CLICK_DECLS

IPIn::IPIn()
{

}

int IPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void IPIn::push_batch(int port, PacketBatch* flow)
{
    EXECUTE_FOR_EACH_PACKET([this](Packet* p){
        WritablePacket *packet = p->uniqueify();

        setAnnotationModification(packet, false);

        // Compute the offset of the IP payload
        const click_ip *iph = packet->ip_header();
        unsigned iph_len = iph->ip_hl << 2;
        uint16_t offset = (uint16_t) (packet->network_header() + iph_len - packet->data());
        setContentOffset(packet, offset);
        return packet;
    }, flow);
    output(0).push_batch(flow);
}

void IPIn::setPacketModified(WritablePacket* p)
{
    // Annotate the packet to indicate it has been modified
    // While going through "out elements", the checksum will be recomputed
    setAnnotationModification(p, true);

    // Continue in the stack function
    StackElement::setPacketModified(p);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPIn)
//ELEMENT_MT_SAFE(IPIn)
