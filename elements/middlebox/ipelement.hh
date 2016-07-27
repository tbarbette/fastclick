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
 * from it to manage IP packets.
 */
class IPElement
{
public:
    /** @brief Return the length of the IP packet obtained from the header
     * @param packet The IP packet
     * @return The length of the given IP packet obtained from the IP header
     */
    uint16_t packetTotalLength(Packet* packet) const;

    /** @brief Return the offset in the packet at which the IP header starts
     * @param packet The IP packet
     * @return The offset in the packet at which the IP header starts
     */
    uint16_t getIPHeaderOffset(Packet* packet) const;

    /** @brief Set the length of the IP packet in the header
     * @param packet The IP packet
     * @param length The new length of the given IP packet
     */
    void setPacketTotalLength(WritablePacket* packet, unsigned length) const;

    /** @brief Return the IP destination address of the packet
     * @param packet The IP packet
     * @return The IP destination address of the packet
     */
    const uint32_t getDestinationAddress(Packet* packet) const;

    /** @brief Return the IP source address of the packet
     * @param packet The IP packet
     * @return The IP source address of the packet
     */
    const uint32_t getSourceAddress(Packet* packet) const;

    /** @brief Recompute the IP checksum of the packet and set it in the IP header
     * @param packet The IP packet
     */
    void computeIPChecksum(WritablePacket* packet) const;
};

CLICK_ENDDECLS
#endif
