#ifndef MIDDLEBOX_TCPOUT_HH
#define MIDDLEBOX_TCPOUT_HH
#include "tcpelement.hh"
#include <click/element.hh>
#include <click/bytestreammaintainer.hh>

// Forward declaration
class TCPIn;

CLICK_DECLS

class TCPOut : public TCPElement
{
public:
    TCPOut() CLICK_COLD;

    const char *class_name() const        { return "TCPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void setInElement(TCPIn*);

protected:
    Packet* processPacket(struct fcb*, Packet*);
    TCPIn* inElement;
};

CLICK_ENDDECLS
#endif
