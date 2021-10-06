/*
 * udpin.{cc,hh} -- entry point of an IP path in the stack of the middlebox
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/udp.h>
#include "udpin.hh"

CLICK_DECLS

UDPIn::UDPIn()
{

}

int UDPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void UDPIn::push_batch(int port, PacketBatch* flow)
{
    EXECUTE_FOR_EACH_PACKET([this](Packet* packet){
        packet->setContentOffset(packet->getContentOffset() + sizeof(click_udp));
        return packet;
    }, flow);
    output(0).push_batch(flow);
}

void UDPIn::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    uint16_t contentOffset = packet->getContentOffset();

    if(position + contentOffset > packet->length())
    {
        click_chatter("Error: Invalid removeBytes call (packet length: %u, position: %u)",
            packet->length(), position);
        return;
    }

    // As opposed to TCP, we directly change the IP length as we have no modification map
    setPacketTotalLength(packet, packetTotalLength(packet) - length);

    // Continue in the stack function
    CTXElement::removeBytes(packet, position, length);
}

WritablePacket* UDPIn::insertBytes(WritablePacket* packet, uint32_t position,
     uint32_t length)
{
    uint16_t contentOffset = packet->getContentOffset();
    if(position + contentOffset > packet->length())
    {
        click_chatter("Error: Invalid removeBytes call (packet length: %u, position: %u)",
            packet->length(), position);
        return packet;
    }

    setPacketTotalLength(packet, packetTotalLength(packet) + length);

    return CTXElement::insertBytes(packet, position, length);
}
CLICK_ENDDECLS
EXPORT_ELEMENT(UDPIn)
ELEMENT_MT_SAFE(UDPIn)
