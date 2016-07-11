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
    WritablePacket* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags, uint32_t contentSize = 0);
    uint16_t getDestinationPort(Packet*);
    uint16_t getSourcePort(Packet*);
    tcp_seq_t getSequenceNumber(Packet*) const;
    tcp_seq_t getAckNumber(Packet*);
    uint16_t getWindowSize(Packet *packet);
    void setWindowSize(WritablePacket *packet, uint16_t winSize);
    bool isSyn(Packet* packet);
    bool isFin(Packet* packet);
    bool isRst(Packet* packet);
    bool isAck(Packet* packet);
    bool checkFlag(Packet *packet, uint8_t flag);
    unsigned getPayloadLength(Packet*);
    unsigned char* getPayload(WritablePacket* packet);
    const unsigned char* getPayloadConst(Packet* packet);
    uint16_t getPayloadOffset(Packet* packet);
    void setPayload(WritablePacket* packet, const unsigned char* payload, uint32_t length);
    void setSequenceNumber(WritablePacket*, tcp_seq_t);
    void setAckNumber(WritablePacket*, tcp_seq_t);
    bool isJustAnAck(Packet* packet);
    uint8_t getFlags(Packet *packet);
    void computeTCPChecksum(WritablePacket*);
};

CLICK_ENDDECLS

#endif
