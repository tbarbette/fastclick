/*
 * tcpelement.hh - Provides several methods that can be used by elements to manage TCP packets
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/element.hh>
#include "ipelement.hh"

CLICK_DECLS

/**
 * @class TCPElement
 * @brief This class provides several methods that can be used by elements that inherits
 * from it to manage TCP packets.
 */

class TCPElement : public IPElement
{
public:

    /**
     * @brief Create a TCP packet
     * @param saddr Source IP address
     * @param daddr Destination IP address
     * @param sport Source port
     * @param dport Destination port
     * @param seq Sequence number
     * @param ack Ack number
     * @param windowSize Window size
     * @param flags The TCP flags on one byte
     * @param contentSize Extra space to allocate for the TCP payload
     * @return The created TCP packet
     */
    WritablePacket* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
        tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags,
        uint32_t contentSize = 0) const;

    /**
     * @brief Return the destination port of a packet
     * @param packet The packet
     * @return The destination port of the packet
     */
    uint16_t getDestinationPort(Packet* packet) const;

    /**
     * @brief Return the source port of a packet
     * @param packet The packet
     * @return The source port of the packet
     */
    uint16_t getSourcePort(Packet* packet) const;

    /**
     * @brief Return the sequence number of a packet
     * @param packet The packet
     * @return The sequence number of the packet
     */
    tcp_seq_t getSequenceNumber(Packet* packet) const;

    /**
     * @brief Return the ack number of a packet
     * @param packet The packet
     * @return The ack number of the packet
     */
    tcp_seq_t getAckNumber(Packet* packet) const;

    /**
     * @brief Return the window size set in the header of a packet
     * @param packet The packet
     * @return The window size set in the header of the packet
     */
    uint16_t getWindowSize(Packet *packet) const;

    /**
     * @brief Set the window size in the TCP header of a packet
     * @param packet The packet
     * @param winSize The window size
     */
    void setWindowSize(WritablePacket *packet, uint16_t winSize) const;

    /**
     * @brief Indicate whether a packet is a SYN packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a SYN packet
     */
    bool isSyn(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is a FIN packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a FIN packet
     */
    bool isFin(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is a RST packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a RST packet
     */
    bool isRst(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is an ACK packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is an ACK packet
     */
    bool isAck(Packet* packet) const;

    /**
     * @brief Check if a given flag is set in a packet
     * @param packet The packet
     * @param flag The offset of the flag
     * @return A boolean indicating whether the flag is set in the packet
     */
    bool checkFlag(Packet *packet, uint8_t flag) const;

    /**
     * @brief Return the length of the TCP payload of a packet
     * @param packet The packet
     * @return The length of the TCP payload of the packet
     */
    unsigned getPayloadLength(Packet* packet) const;

    /**
     * @brief Return the payload of a TCP packet
     * @param packet The packet
     * @return The payload of the packet
     */
    unsigned char* getPayload(WritablePacket* packet) const;

    /**
     * @brief Return the payload of a TCP packet
     * @param packet The packet
     * @return The const payload of the packet
     */
    const unsigned char* getPayloadConst(Packet* packet) const;

    /**
     * @brief Return the offset of the payload in a TCP packet
     * @param packet The packet
     * @return The offset of the payload in a TCP packet
     */
    uint16_t getPayloadOffset(Packet* packet) const;

    /**
     * @brief Set the payload of a TCP packet
     * @param packet The packet
     * @param payload The new value of the payload
     * @param length The length of the payload
     */
    void setPayload(WritablePacket* packet, const unsigned char* payload, uint32_t length) const;

    /**
     * @brief Set the sequence number of a TCP packet
     * @param packet The packet
     * @param seq The sequence number of the packet
     */
    void setSequenceNumber(WritablePacket* packet, tcp_seq_t seq) const;

    /**
     * @brief Set the ack number of a TCP packet
     * @param packet The packet
     * @param ack The ack number of the packet
     */
    void setAckNumber(WritablePacket* packet, tcp_seq_t ack) const;

    /**
     * @brief Indicate whether the packet is just an ACK without any additional information
     * @param packet The packet
     * @return A boolean indicating whether the packet is just an ACK without any additional
     * information
     */
    bool isJustAnAck(Packet* packet) const;

    /**
     * @brief Return the flags of a TCP packet
     * @param packet The packet
     * @return A byte containing the flags of the packet
     */
    uint8_t getFlags(Packet* packet) const;

    /**
     * @brief Compute the TCP checksum of a packet and set it in its header
     * @param packet The packet
     */
    void computeTCPChecksum(WritablePacket* packet) const;
};

CLICK_ENDDECLS

#endif
