#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/ip.h>
#include "ipelement.hh"

CLICK_DECLS

uint16_t IPElement::packetTotalLength(Packet *packet)
{
    const click_ip *iph = packet->ip_header();

    return ntohs(iph->ip_len);
}

uint16_t IPElement::getIPHeaderOffset(Packet *packet)
{
    return (((const unsigned char *)packet->ip_header()) - packet->data());
}

void IPElement::setPacketTotalLength(WritablePacket* packet, unsigned length)
{
    click_ip *iph = packet->ip_header();
    iph->ip_len = htons(length);
}


void IPElement::computeIPChecksum(WritablePacket *packet)
{
    click_ip *iph = packet->ip_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    unsigned hlen = iph->ip_hl << 2;

    iph->ip_sum = 0;
    iph->ip_sum = click_in_cksum((const unsigned char *)iph, hlen);
}

const uint32_t IPElement::getSourceAddress(Packet* packet)
{
    const click_ip *iph = packet->ip_header();

    return *(const uint32_t*)&iph->ip_src;
}

const uint32_t IPElement::getDestinationAddress(Packet* packet)
{
    const click_ip *iph = packet->ip_header();

    return *(const uint32_t*)&iph->ip_dst;
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(IPElement)
