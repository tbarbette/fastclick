#include <click/config.h>
#include "tcpelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>

CLICK_DECLS

TCPElement::TCPElement()
{

}

int TCPElement::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void TCPElement::computeChecksum(WritablePacket *packet)
{
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    tcph->th_sum = 0;
    unsigned csum = click_in_cksum((unsigned char *)tcph, plen);
    tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
}

void TCPElement::setSequenceNumber(WritablePacket* packet, tcp_seq_t seq)
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_seq = htonl(seq);
}

tcp_seq_t TCPElement::getSequenceNumber(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
}

tcp_seq_t TCPElement::getAckNumber(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_ack);
}

void TCPElement::setAckNumber(WritablePacket* packet, tcp_seq_t ack)
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_ack = htonl(ack);
}

unsigned TCPElement::getPacketLength(Packet* packet)
{
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    return ip_len - iph_len - tcp_offset;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPElement)
//ELEMENT_MT_SAFE(TCPElement)
