#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <clicknet/ether.h>
#include "tcpelement.hh"
#include "ipelement.hh"

CLICK_DECLS

void TCPElement::computeTCPChecksum(WritablePacket *packet)
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

uint16_t TCPElement::getWindowSize(Packet *packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_win);
}

void TCPElement::setWindowSize(WritablePacket *packet, uint16_t winSize)
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_win = htons(winSize);
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

const unsigned char* TCPElement::getPayload(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    return (const unsigned char*)packet->transport_header() + tcph_len;
}

Packet* TCPElement::forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport,
                             uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags)
{
    struct click_ether *ether; // Ethernet header
    struct click_ip *ip;       // IP header
    struct click_tcp *tcp;     // TCP header

    // Build a new packet
    WritablePacket *packet = Packet::make(sizeof(struct click_ether)
        + sizeof(struct click_ip) + sizeof(struct click_tcp));

    assert(packet != NULL);

    // Clean the data of the packet
    memset(packet->data(), '\0', packet->length());

    ether = (struct click_ether*)packet->data();
    ip = (struct click_ip*)(ether + 1);
    tcp = (struct click_tcp*)(ip + 1);
    packet->set_ip_header(ip, sizeof(struct click_ip));

    // Set a blank ethernet header
    ether->ether_type = htons(ETHERTYPE_IP); // Indicate it is an IP packet
    uint8_t etherBlank[6] = {0x0};
    memcpy((void*)&(ether->ether_dhost), (void*)&etherBlank, sizeof(etherBlank));
    memcpy((void*)&(ether->ether_shost), (void*)&etherBlank, sizeof(etherBlank));

    // Set the IP header
    ip->ip_v = 4; // IPv4
    ip->ip_hl = 5; // Set IP header length (no options and 5 is the minimum value)
    ip->ip_tos = 0; // Set type of service to 0
    // Set the current length of the packet (with empty TCP payload)
    ip->ip_len = htons(packet->length() - sizeof(struct click_ether));
    ip->ip_id = htons(0);      // Set fragment id to 0
    ip->ip_off = htons(IP_DF); // Indicate not to fragment
    ip->ip_ttl = 255;          // Set the TTL to 255
    ip->ip_p = IP_PROTO_TCP; // Indicate that it is a TCP packet
    ip->ip_sum = 0; // Set the IP checksum to 0 (it will be computed afterwards)
    // Set the IP addresses
    memcpy((void*)&(ip->ip_src), (void*)&saddr, sizeof(saddr));
    memcpy((void*)&(ip->ip_dst), (void*)&daddr, sizeof(daddr));

    // Set the TCP ports
    memcpy((void*)&(tcp->th_sport), (void*)&sport, sizeof(sport));
    memcpy((void*)&(tcp->th_dport), (void*)&dport, sizeof(dport));
    tcp->th_seq = htonl(seq); // Set the sequence number
    tcp->th_ack = htonl(ack); // Set the ack number
    tcp->th_off = 5; // Set the data offset
    tcp->th_flags = flags; // Set the flags
    tcp->th_win = htons(winSize); // Set the window size
    tcp->th_sum = htons(0); // Set temporarily the checksum to 0
    tcp->th_urp = htons(0); // Set urgent pointer to 0

    // Pull the ethernet header
    packet->pull(14);

    // Finally compute the checksums
    computeTCPChecksum(packet);
    computeIPChecksum(packet);

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

void TCPElement::setFlowDirection(unsigned int flowDirection)
{
    this->flowDirection = flowDirection;
}

unsigned int TCPElement::getFlowDirection()
{
    return flowDirection;
}

unsigned int TCPElement::getOppositeFlowDirection()
{
    return (1 - flowDirection);
}

bool TCPElement::isSyn(Packet* packet)
{
    return checkFlag(packet, TH_SYN);
}

bool TCPElement::isFin(Packet* packet)
{
    return checkFlag(packet, TH_FIN);
}

bool TCPElement::isRst(Packet* packet)
{
    return checkFlag(packet, TH_RST);
}

bool TCPElement::isAck(Packet* packet)
{
    return checkFlag(packet, TH_ACK);
}

bool TCPElement::checkFlag(Packet *packet, uint8_t flag)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet has the given flag
    if(flags & flag)
        return true;
    else
        return false;
}

bool TCPElement::isJustAnAck(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // If we have a payload, we are more than just a ACK
    if(getPayloadLength(packet) > 0)
        return false;

    //  If we have other flags, we are more than just an ACK
    if(flags == TH_ACK)
        return true;
    else
        return false;
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(TCPElement)
ELEMENT_REQUIRES(IPElement)
