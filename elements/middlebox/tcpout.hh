#ifndef MIDDLEBOX_TCPOUT_HH
#define MIDDLEBOX_TCPOUT_HH
#include <click/element.hh>
#include <click/bytestreammaintainer.hh>
#include "tcpelement.hh"

// Forward declaration
class TCPIn;

CLICK_DECLS

class TCPOut : public StackElement, TCPElement
{
public:
    TCPOut() CLICK_COLD;

    const char *class_name() const        { return "TCPOut"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    bool isOutElement()                   { return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void setInElement(TCPIn*);

    void push_batch(int, PacketBatch*) override;

protected:

    TCPIn* inElement;
    friend class TCPIn;
};

CLICK_ENDDECLS
#endif
