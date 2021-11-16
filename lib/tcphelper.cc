// -*- c-basic-offset: 4; related-file-name: "../include/click/tcphelper.hh"-*-
/*
 * tcphelper.{cc,hh} -- utility routines for TCP packets
 * Tom Barbette, Romain Gaillard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <clicknet/ether.h>
#include <click/tcphelper.hh>
#include <click/ipelement.hh>

CLICK_DECLS

TCPHelper::TCPHelper()
{

}
WritablePacket* TCPHelper::forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport,
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

int TCPHelper::iterateOptions(Packet *packet, std::function<bool(uint8_t, void*)> fnt)
{
	const click_tcp *tcph = packet->tcp_header();

	uint8_t *optStart = const_cast<uint8_t *>((uint8_t *) (tcph + 1));
	uint8_t *optEnd = const_cast<uint8_t *>((uint8_t *) tcph + (tcph->th_off << 2));

	if(optEnd > packet->end_data())
		optEnd = const_cast<uint8_t *>(packet->end_data());

	int n = 0;

	while(optStart < optEnd)
	{
		if(optStart[0] == TCPOPT_EOL) // End of list
			break; // Stop searching
		else if(optStart[0] == TCPOPT_NOP)
			optStart += 1; // Move to the next option
		else if(optStart[1] < 2 || optStart[1] + optStart > optEnd) {
			return -1; // Avoid malformed options
		}
		/*else if(optStart[0] == TCPOPT_SACK_PERMITTED && optStart[1] == TCPOLEN_SACK_PERMITTED)
		{
			fnt(TCPOPT_SACK_PERMITTED,(void*)&optStart[2]);
			optStart += optStart[1];
		}
		else if(optStart[0] == TCPOPT_WSCALE && optStart[1] == TCPOLEN_WSCALE)
		{
			fnt(TCPOPT_WSCALE,(void*)&optStart[2]);
			optStart += optStart[1];
		}
		else if(optStart[0] == TCPOPT_MAXSEG && optStart[1] == TCPOLEN_MAXSEG)
		{
			fnt(TCPOPT_MAXSEG,(void*)&optStart[2]);
			optStart += optStart[1];
		}*/
		else
		{
			fnt(optStart[0],(void*)&optStart[2]);
			optStart += optStart[1];
			return -1;
		}

		n++;
	}
	return n;
}

CLICK_ENDDECLS
