/*
 * ipin.{cc,hh} -- entry point of an IP path in the stack of the middlebox
 * Romain Gaillard
 * Tom Barbette
 */

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
    EXECUTE_FOR_EACH_PACKET([this](Packet* packet){
        // Compute the offset of the IP payload
        const click_ip *iph = packet->ip_header();
        unsigned iph_len = iph->ip_hl << 2;
        uint16_t offset = (uint16_t)(packet->network_header() + iph_len - packet->data());
        setContentOffset(packet, offset);
        return packet;
    }, flow);
    output(0).push_batch(flow);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPIn)
ELEMENT_REQUIRES(IPElement)
ELEMENT_MT_SAFE(IPIn)
