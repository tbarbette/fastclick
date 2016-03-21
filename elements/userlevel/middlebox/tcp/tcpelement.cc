#include <click/config.h>
#include "tcpelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <clicknet/ether.h>

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

unsigned TCPElement::getPayloadLength(Packet* packet)
{
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    return ip_len - iph_len - tcp_offset;
}

Packet* TCPElement::forgeAck(uint32_t saddr, uint32_t daddr, uint16_t sport,
                             uint16_t dport, tcp_seq_t seq, tcp_seq_t ack)
{
    struct click_ether *ether;
    struct click_ip *ip;
    struct click_tcp *tcp;
    WritablePacket *packet = Packet::make(sizeof(struct click_ether) + sizeof(struct click_ip) + sizeof(struct click_tcp));

    if(packet == NULL)
        return NULL;
        
    memset(packet->data(), '\0', packet->length());

    ether = (struct click_ether*)packet->data();
    ip = (struct click_ip*)(ether + 1);
    tcp = (struct click_tcp*)(ip + 1);
    packet->set_ip_header(ip, sizeof(struct click_ip));

    ether->ether_type = htons(ETHERTYPE_IP);
    uint8_t etherDest[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy((void*)&(ether->ether_dhost), (void*)&etherDest, sizeof(etherDest));

    ip->ip_v = 4;
    ip->ip_hl = 5;
    ip->ip_tos = 0;
    ip->ip_len = htons(packet->length() - sizeof(struct click_ether));
    ip->ip_id = htons(0);
    ip->ip_off = htons(IP_DF);
    ip->ip_ttl = 255;
    ip->ip_p = IP_PROTO_TCP;
    ip->ip_sum = 0;
    memcpy((void*)&(ip->ip_src), (void*)&saddr, sizeof(saddr));
    memcpy((void*)&(ip->ip_dst), (void*)&daddr, sizeof(daddr));

    memcpy((void*)&(tcp->th_sport), (void*)&sport, sizeof(sport));
    memcpy((void*)&(tcp->th_dport), (void*)&dport, sizeof(dport));
    tcp->th_seq = htonl(seq);
    tcp->th_ack = htonl(ack);
    tcp->th_off = 5;
    tcp->th_flags = TH_ACK;
    tcp->th_win = htons(32120);
    tcp->th_sum = htons(0);
    tcp->th_urp = htons(0);

    packet->pull(14);

    computeChecksum(packet);
    setAnnotationModification(packet, true);

    return packet;
}

const uint16_t TCPElement::getSourcePort(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return (uint16_t)tcph->th_sport;
}

const uint16_t TCPElement::getDestinationPort(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return (uint16_t)tcph->th_dport;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPElement)
//ELEMENT_MT_SAFE(TCPElement)
