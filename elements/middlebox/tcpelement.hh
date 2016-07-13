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
    WritablePacket* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags, uint32_t contentSize = 0) const;
    uint16_t getDestinationPort(Packet*) const;
    uint16_t getSourcePort(Packet*) const;
    tcp_seq_t getSequenceNumber(Packet*) const;
    tcp_seq_t getAckNumber(Packet*) const;
    uint16_t getWindowSize(Packet *packet) const;
    void setWindowSize(WritablePacket *packet, uint16_t winSize) const;
    bool isSyn(Packet* packet) const;
    bool isFin(Packet* packet) const;
    bool isRst(Packet* packet) const;
    bool isAck(Packet* packet) const;
    bool checkFlag(Packet *packet, uint8_t flag) const;
    unsigned getPayloadLength(Packet*) const;
    unsigned char* getPayload(WritablePacket* packet) const;
    const unsigned char* getPayloadConst(Packet* packet) const;
    uint16_t getPayloadOffset(Packet* packet) const;
    void setPayload(WritablePacket* packet, const unsigned char* payload, uint32_t length) const;
    void setSequenceNumber(WritablePacket*, tcp_seq_t) const;
    void setAckNumber(WritablePacket*, tcp_seq_t) const;
    bool isJustAnAck(Packet* packet) const;
    uint8_t getFlags(Packet *packet) const;
    void computeTCPChecksum(WritablePacket*) const;
};

CLICK_ENDDECLS

#endif
