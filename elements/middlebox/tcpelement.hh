#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "stackelement.hh"

CLICK_DECLS

class TCPElement
{
public:
    TCPElement() CLICK_COLD;


    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    unsigned getPayloadLength(Packet*);
    void setSequenceNumber(WritablePacket*, tcp_seq_t);
    void setAckNumber(WritablePacket*, tcp_seq_t);
    Packet* forgePacket(uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, uint8_t);
    static const uint16_t getDestinationPort(Packet*);
    static const uint16_t getSourcePort(Packet*);
    static tcp_seq_t getSequenceNumber(Packet*);
    static tcp_seq_t getAckNumber(Packet*);

protected:
    unsigned int flowDirection;

    void computeChecksum(WritablePacket*);
    void setFlowDirection(unsigned int flowDirection);
    unsigned int getFlowDirection();
    unsigned int getOppositeFlowDirection();

    friend class TCPIn;
};

CLICK_ENDDECLS
#endif
