#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include "tcpelement.hh"
#include "tcpout.hh"
#include <click/element.hh>
CLICK_DECLS

class TCPIn : public TCPElement
{
public:
    TCPIn() CLICK_COLD;

    const char *class_name() const        { return "TCPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    TCPOut* getOutElement();
    TCPIn* getReturnElement();

protected:
    Packet* processPacket(Packet*);
    void packetModified(Packet*);

    TCPOut* outElement;
    TCPIn* returnElement;

};

CLICK_ENDDECLS
#endif
