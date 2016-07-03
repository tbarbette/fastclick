#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "stackelement.hh"

CLICK_DECLS

class TCPElement : public StackElement
{
public:
    TCPElement() CLICK_COLD;

    const char *class_name() const        { return "TCPElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    Packet* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags);
    static const uint16_t getDestinationPort(Packet*);
    static const uint16_t getSourcePort(Packet*);
    static tcp_seq_t getSequenceNumber(Packet*);
    static tcp_seq_t getAckNumber(Packet*);
    static uint16_t getWindowSize(Packet *packet);
    static void setWindowSize(WritablePacket *packet, uint16_t winSize);
    static bool isSyn(Packet* packet);
    static bool isFin(Packet* packet);
    static bool isRst(Packet* packet);
    static bool isAck(Packet* packet);
    static bool checkFlag(Packet *packet, uint8_t flag);
    static unsigned getPayloadLength(Packet*);
    static void setSequenceNumber(WritablePacket*, tcp_seq_t);
    static void setAckNumber(WritablePacket*, tcp_seq_t);
    static bool isJustAnAck(Packet* packet);
    static void computeChecksum(WritablePacket*);

protected:
    unsigned int flowDirection;

    void setFlowDirection(unsigned int flowDirection);
    unsigned int getFlowDirection();
    unsigned int getOppositeFlowDirection();

    friend class TCPIn;
};

CLICK_ENDDECLS
#endif
