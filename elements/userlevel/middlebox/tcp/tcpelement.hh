#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH
#include "../stackelement.hh"
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>

CLICK_DECLS

class TCPElement : public StackElement
{
public:
    TCPElement() CLICK_COLD;

    const char *class_name() const        { return "TCPElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    unsigned getPayloadLength(Packet*);
    tcp_seq_t getSequenceNumber(Packet*);
    tcp_seq_t getAckNumber(Packet*);
    void setSequenceNumber(WritablePacket*, tcp_seq_t);
    void setAckNumber(WritablePacket*, tcp_seq_t);
    Packet* forgeAck(uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t);
    static const uint16_t getDestinationPort(Packet*);
    static const uint16_t getSourcePort(Packet*);

protected:
    void computeChecksum(WritablePacket*);
};

CLICK_ENDDECLS
#endif
