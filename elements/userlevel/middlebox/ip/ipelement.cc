#include <click/config.h>
#include "ipelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>

CLICK_DECLS

IPElement::IPElement()
{

}

int IPElement::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

uint16_t IPElement::packetTotalLength(Packet *packet)
{
    const click_ip *iph = packet->ip_header();

    return ntohs(iph->ip_len);
}

void IPElement::setPacketTotalLength(WritablePacket* packet, unsigned length)
{
    click_ip *iph = packet->ip_header();
    iph->ip_len = htons(length);
}


void IPElement::computeChecksum(WritablePacket *packet)
{
    click_ip *iph = packet->ip_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    unsigned hlen = iph->ip_hl << 2;

    iph->ip_sum = 0;
    iph->ip_sum = click_in_cksum((const unsigned char *)iph, hlen);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPElement)
//ELEMENT_MT_SAFE(IPElement)
