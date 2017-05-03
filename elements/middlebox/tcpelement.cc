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

TCPElement::TCPElement()
{

}


void TCPElement::computeTCPChecksum(WritablePacket *packet) const
{
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    tcph->th_sum = 0;
    unsigned csum = click_in_cksum((unsigned char *)tcph, plen);
    tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
}

void TCPElement::setSequenceNumber(WritablePacket* packet, tcp_seq_t seq) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_seq = htonl(seq);
}

tcp_seq_t TCPElement::getSequenceNumber(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
}

tcp_seq_t TCPElement::getAckNumber(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_ack);
}

void TCPElement::setAckNumber(WritablePacket* packet, tcp_seq_t ack) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_ack = htonl(ack);
}

uint16_t TCPElement::getWindowSize(Packet *packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_win);
}

void TCPElement::setWindowSize(WritablePacket *packet, uint16_t winSize) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_win = htons(winSize);
}

unsigned TCPElement::getPayloadLength(Packet* packet) const
{
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    return ip_len - iph_len - tcp_offset;
}

unsigned char* TCPElement::getPayload(WritablePacket* packet) const
{
    click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    return (unsigned char*)packet->transport_header() + tcph_len;
}

const unsigned char* TCPElement::getPayloadConst(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    return (const unsigned char*)packet->transport_header() + tcph_len;
}

void TCPElement::setPayload(WritablePacket* packet, const unsigned char* payload,
    uint32_t length) const
{
    click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;

    unsigned char* payloadPtr = (unsigned char*)packet->transport_header() + tcph_len;
    memcpy(payloadPtr, payload, length);
}

uint16_t TCPElement::getPayloadOffset(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    uint16_t offset = (uint16_t)(packet->transport_header() + tcph_len - packet->data());

    return offset;
}

WritablePacket* TCPElement::forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport,
    uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags,
    uint32_t contentSize) const
{
    struct click_ip *ip;       // IP header
    struct click_tcp *tcp;     // TCP header

    // Build a new packet
    WritablePacket *packet = Packet::make(sizeof(struct click_ip)
        + sizeof(struct click_tcp) + contentSize);

    assert(packet != NULL);

    // Clean the data of the packet
    memset(packet->data(), '\0', packet->length());

    ip = (struct click_ip*)packet->data();
    tcp = (struct click_tcp*)(ip + 1);
    packet->set_ip_header(ip, sizeof(struct click_ip));

    // Set the IP header
    ip->ip_v = 4; // IPv4
    ip->ip_hl = 5; // Set IP header length (no options and 5 is the minimum value)
    ip->ip_tos = 0; // Set type of service to 0
    // Set the current length of the packet
    ip->ip_len = htons(packet->length());
    ip->ip_id = htons(0);      // Set fragment id to 0
    ip->ip_off = htons(IP_DF); // Indicate not to fragment
    ip->ip_ttl = 255;          // Set the TTL to 255
    ip->ip_p = IP_PROTO_TCP; // Indicate that it is a TCP packet
    ip->ip_sum = 0; // Set the IP checksum to 0 (it will be computed afterwards)
    // Set the IP addresses
    memcpy((void*)&(ip->ip_src), (void*)&saddr, sizeof(saddr));
    memcpy((void*)&(ip->ip_dst), (void*)&daddr, sizeof(daddr));

    // Set the TCP ports
    tcp->th_sport = htons(sport);
    tcp->th_dport = htons(dport);
    tcp->th_seq = htonl(seq); // Set the sequence number
    tcp->th_ack = htonl(ack); // Set the ack number
    tcp->th_off = 5; // Set the data offset
    tcp->th_flags = flags; // Set the flags
    tcp->th_win = htons(winSize); // Set the window size
    tcp->th_sum = htons(0); // Set temporarily the checksum to 0
    tcp->th_urp = htons(0); // Set urgent pointer to 0

    // Finally compute the checksums
    computeTCPChecksum(packet);
    computeIPChecksum(packet);

    packet->set_dst_ip_anno(daddr);

    return packet;
}

uint16_t TCPElement::getSourcePort(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_sport);
}

uint16_t TCPElement::getDestinationPort(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_dport);
}

bool TCPElement::isSyn(Packet* packet) const
{
    return checkFlag(packet, TH_SYN);
}

bool TCPElement::isFin(Packet* packet) const
{
    return checkFlag(packet, TH_FIN);
}

bool TCPElement::isRst(Packet* packet) const
{
    return checkFlag(packet, TH_RST);
}

bool TCPElement::isAck(Packet* packet) const
{
    return checkFlag(packet, TH_ACK);
}

bool TCPElement::checkFlag(Packet *packet, uint8_t flag) const
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet has the given flag
    if(flags & flag)
        return true;
    else
        return false;
}

uint8_t TCPElement::getFlags(Packet *packet) const
{
    const click_tcp *tcph = packet->tcp_header();
    return tcph->th_flags;
}

bool TCPElement::isJustAnAck(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // If we have a payload, we are more than just an ACK
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
