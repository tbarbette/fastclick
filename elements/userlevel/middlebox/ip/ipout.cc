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
    {
        click_ip *iph = packet->ip_header();

        unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
        unsigned hlen = iph->ip_hl << 2;

        iph->ip_sum = 0;
        iph->ip_sum = click_in_cksum((const unsigned char *)iph, hlen);
        click_chatter("IPOut recomputed the checksum");
    }

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPOut)
//ELEMENT_MT_SAFE(IPOut)
