/*
 * ipelement.hh - Provides several methods that can be used by elements to manage IP packets
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_IPELEMENT_HH
#define MIDDLEBOX_IPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>

CLICK_DECLS

/**
 * @class IPElement
 * @brief This class provides several methods that can be used by elements that inherits
 * from it in order to manage IP packets.
 */
class IPElement
{
public:
    /** @brief Return the length of the IP packet, obtained from the header
     * @param packet The IP packet
     * @return The length of the given IP packet, obtained from the IP header
     */
    inline uint16_t packetTotalLength(Packet* packet) const;

    /** @brief Return the offset in the packet at which the IP header starts
     * @param packet The IP packet
     * @return The offset in the packet at which the IP header starts
     */
    inline uint16_t getIPHeaderOffset(Packet* packet) const;

    /** @brief Set the length of the IP packet in the header
     * @param packet The IP packet
     * @param length The new length of the given IP packet
     */
    inline void setPacketTotalLength(WritablePacket* packet, unsigned length) const;

    /** @brief Return the IP destination address of the packet
     * @param packet The IP packet
     * @return The IP destination address of the packet
     */
    inline const uint32_t getDestinationAddress(Packet* packet) const;

    /** @brief Return the IP source address of the packet
     * @param packet The IP packet
     * @return The IP source address of the packet
     */
    inline const uint32_t getSourceAddress(Packet* packet) const;

    /** @brief Recompute the IP checksum of the packet and set it in the IP header
     * @param packet The IP packet
     */
    inline void computeIPChecksum(WritablePacket* packet) const;
};

inline uint16_t IPElement::packetTotalLength(Packet *packet) const
{
    const click_ip *iph = packet->ip_header();

    return ntohs(iph->ip_len);
}

inline uint16_t IPElement::getIPHeaderOffset(Packet *packet) const
{
    return (((const unsigned char *)packet->ip_header()) - packet->data());
}

inline void IPElement::setPacketTotalLength(WritablePacket* packet, unsigned length) const
{
    click_ip *iph = packet->ip_header();
    iph->ip_len = htons(length);
}


inline void IPElement::computeIPChecksum(WritablePacket *packet) const
{
    click_ip *iph = packet->ip_header();

    //unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    unsigned hlen = iph->ip_hl << 2;

    iph->ip_sum = 0;
    iph->ip_sum = click_in_cksum((const unsigned char *)iph, hlen);
}

inline const uint32_t IPElement::getSourceAddress(Packet* packet) const
{
    const click_ip *iph = packet->ip_header();

    return *(const uint32_t*)&iph->ip_src;
}

inline const uint32_t IPElement::getDestinationAddress(Packet* packet) const
{
    const click_ip *iph = packet->ip_header();

    return *(const uint32_t*)&iph->ip_dst;
}

CLICK_ENDDECLS
#endif
