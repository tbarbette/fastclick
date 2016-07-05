#ifndef MIDDLEBOX_IPELEMENT_HH
#define MIDDLEBOX_IPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>

CLICK_DECLS

class IPElement
{
public:
    uint16_t packetTotalLength(Packet*);
    uint16_t getIPHeaderOffset(Packet* packet);
    void setPacketTotalLength(WritablePacket*, unsigned);
    const uint32_t getDestinationAddress(Packet*);
    const uint32_t getSourceAddress(Packet*);
    void computeIPChecksum(WritablePacket*);

protected:
};

CLICK_ENDDECLS
#endif
