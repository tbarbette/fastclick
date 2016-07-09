#ifndef MIDDLEBOX_TCPOUT_HH
#define MIDDLEBOX_TCPOUT_HH
#include <click/element.hh>
#include "stackelement.hh"
#include "bytestreammaintainer.hh"
#include "tcpelement.hh"

// Forward declaration
class TCPIn;

CLICK_DECLS

class TCPOut : public StackElement, public TCPElement
{
public:
    TCPOut() CLICK_COLD;

    const char *class_name() const        { return "TCPOut"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void sendAck(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack);

    void setInElement(TCPIn*);

protected:
    Packet* processPacket(struct fcb*, Packet*);
    TCPIn* inElement;
};

CLICK_ENDDECLS
#endif
