#ifndef MIDDLEBOX_IPELEMENT_HH
#define MIDDLEBOX_IPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>

CLICK_DECLS

class IPElement
{
public:
    uint16_t packetTotalLength(Packet*) const;
    uint16_t getIPHeaderOffset(Packet* packet) const;
    void setPacketTotalLength(WritablePacket*, unsigned) const;
    const uint32_t getDestinationAddress(Packet*) const;
    const uint32_t getSourceAddress(Packet*) const;
    void computeIPChecksum(WritablePacket*) const;

protected:
};

CLICK_ENDDECLS
#endif
