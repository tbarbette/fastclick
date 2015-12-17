#include <click/config.h>
#include "tcpout.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>

CLICK_DECLS

TCPOut::TCPOut()
{

}

int TCPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* TCPOut::processPacket(Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    // Recompute the TCP checksum if the packet has been modified
    if(getAnnotationModification(packet))
    {
        click_ip *iph = packet->ip_header();
        click_tcp *tcph = packet->tcp_header();

        unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
        tcph->th_sum = 0;
        unsigned csum = click_in_cksum((unsigned char *)tcph, plen);
        tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
        click_chatter("TCPOut recomputed the checksum");
    }

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPOut)
//ELEMENT_MT_SAFE(TCPOut)
