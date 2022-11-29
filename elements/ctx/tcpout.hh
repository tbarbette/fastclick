#ifndef MIDDLEBOX_TCPOUT_HH
#define MIDDLEBOX_TCPOUT_HH
#include <click/element.hh>
#include <click/flow/ctxelement.hh>
#include <click/bytestreammaintainer.hh>
#include <click/tcphelper.hh>

// Forward declaration
class TCPIn;

CLICK_DECLS

/*
=c

TCPOut()

=s ctx

exit point of a TCP path in the stack of the middlebox

=d

This element is the exit point of a TCP path in the stack of the middlebox by which all
TCP packets must go after their TCP content has been processed. Each path containing a TCPOut element
must also contain a TCPIn element

The first output corresponds to the normal path. The second output is optional and is used
to send packets back to the source (for instance to acknowledge packets).

=a TCPIn */

class TCPOut : public CTXElement, public TCPHelper
{
public:
    /**
     * @brief Construct a TCPOut element
     */
    TCPOut() CLICK_COLD;

    const char *class_name() const        { return "TCPOut"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    int tcp_initialize(ErrorHandler* errh) CLICK_COLD;

    void push_batch(int port, PacketBatch* flow) override;

    /**
     * @brief Send an ACK packet on the second output
     * @param maintainer ByteStreamMaintainer of the other side of the connection (used to get
     * information such as the window size)
     * @param saddr IP source address
     * @param daddr IP destination address
     * @param sport Source port
     * @param dport Destination port
     * @param seq Sequence number
     * @param ack Ack number
     * @param force Boolean used to force the sending of the ACK even if a previous ACK for the same
     * data has already been sent
     */
    Packet* forgeAck(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr, uint16_t sport,
         uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, bool force = false);

    void sendOpposite(Packet* p);
     /**
      * @brief Send a packet to close the connection on the second output
      * @param maintainer ByteStreamMaintainer of the other side of the connection (used to get
      * information such as the window size)
      * @param saddr IP source address
      * @param daddr IP destination address
      * @param sport Source port
      * @param dport Destination port
      * @param seq Sequence number
      * @param ack Ack number
      * @param graceful Boolean indicating if the connection must be closed gracefully (via a FIN
      * packet) or ungracefully (via a RST packet)
      */
    void sendClosingPacket(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr,
        uint16_t sport, uint16_t dport, int graceful);

    void sendModifiedPacket(WritablePacket* packet) {
            if (!_sw_checksum)
                resetTCPChecksum(packet);
            else
                computeTCPChecksum(packet);
           output_push_batch(0, PacketBatch::make_from_packet(packet)); //Elements never knew about this flow, we bypass

    }

    /**
     * @brief Set the TCPIn element associated
     * @param element A pointer to the TCPIn element associated
     */
    int setInElement(TCPIn* element, ErrorHandler* errh);

    /**
     * @brief Set the flow direction
     * @param flowDirection The flow direction
     */
    void setFlowDirection(unsigned int flowDirection);

    /**
     * @brief Return the flow direction
     * @return The flow direction
     */
    unsigned int getFlowDirection();

    /**
     * @brief Return the flow direction of the other path
     * @return The flow direction of the other path
     */
    unsigned int getOppositeFlowDirection();

protected:
    Packet* processPacket(struct fcb*, Packet*);

    TCPIn* inElement; // TCPIn element of this path

private:
    /**
     * @brief Check whether the connection has been closed or not
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating if the connection is still open
     */
    bool checkConnectionClosed(Packet *packet);

    unsigned int flowDirection;
    bool _readonly;
    bool _allow_resize;
    bool _sw_checksum;
};

CLICK_ENDDECLS
#endif
