#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <clicknet/ether.h>
#include <click/tcpelement.hh>
#include <click/ipelement.hh>

CLICK_DECLS

TCPElement::TCPElement()
{

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

CLICK_ENDDECLS
ELEMENT_PROVIDES(TCPElement)
