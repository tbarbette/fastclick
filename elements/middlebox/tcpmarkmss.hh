#ifndef MIDDLEBOX_TCPMARKMSS_HH
#define MIDDLEBOX_TCPMARKMSS_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <click/element.hh>
#include "fcb.hh"
#include "tcpelement.hh"

CLICK_DECLS

class TCPMarkMSS : public Element, public TCPElement
{
public:
    TCPMarkMSS() CLICK_COLD;

    const char *class_name() const        { return "TCPMarkMSS"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *);
    Packet *pull(int);


private:
    Packet* markMSS(struct fcb *fcb, Packet *packet);

    int8_t annotation;
    int16_t offset;
    unsigned int flowDirection;

    const uint16_t DEFAULT_MSS = 536;
};

CLICK_ENDDECLS

#endif
