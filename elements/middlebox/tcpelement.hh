#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/element.hh>
#include "ipelement.hh"

CLICK_DECLS

class TCPElement : public IPElement
{
public:
    Packet* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags);
     const uint16_t getDestinationPort(Packet*);
     const uint16_t getSourcePort(Packet*);
     tcp_seq_t getSequenceNumber(Packet*);
     tcp_seq_t getAckNumber(Packet*);
     uint16_t getWindowSize(Packet *packet);
     void setWindowSize(WritablePacket *packet, uint16_t winSize);
     bool isSyn(Packet* packet);
     bool isFin(Packet* packet);
     bool isRst(Packet* packet);
     bool isAck(Packet* packet);
     bool checkFlag(Packet *packet, uint8_t flag);
     unsigned getPayloadLength(Packet*);
     const unsigned char* getPayload(Packet* packet);
     void setSequenceNumber(WritablePacket*, tcp_seq_t);
     void setAckNumber(WritablePacket*, tcp_seq_t);
     bool isJustAnAck(Packet* packet);
     void computeTCPChecksum(WritablePacket*);

protected:
    unsigned int flowDirection;

    void setFlowDirection(unsigned int flowDirection);
    unsigned int getFlowDirection();
    unsigned int getOppositeFlowDirection();

    friend class TCPIn;
};

CLICK_ENDDECLS

#endif
