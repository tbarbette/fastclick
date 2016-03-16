#ifndef MIDDLEBOX_TCPOUT_HH
#define MIDDLEBOX_TCPOUT_HH
#include "tcpelement.hh"
#include <click/element.hh>
#include <click/bytestreammaintainer.hh>

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

    ByteStreamMaintainer* getByteStreamMaintainer();

protected:
    Packet* processPacket(Packet*);
    ByteStreamMaintainer byteStreamMaintainer;
};

CLICK_ENDDECLS
#endif
