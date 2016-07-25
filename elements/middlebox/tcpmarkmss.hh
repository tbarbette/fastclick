#ifndef MIDDLEBOX_TCPMARKMSS_HH
#define MIDDLEBOX_TCPMARKMSS_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <click/element.hh>
#include <click/batchelement.hh>
#include "batchfcb.hh"
#include "fcb.hh"
#include "tcpelement.hh"

CLICK_DECLS

class TCPMarkMSS : public BatchElement, public TCPElement
{
public:
    TCPMarkMSS() CLICK_COLD;

    const char *class_name() const        { return "TCPMarkMSS"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_packet(int, Packet *);
    Packet *pull(int);

    #if HAVE_BATCH
    void push_batch(int, PacketBatch *batch);
    PacketBatch * pull_batch(int port, int max);
    #endif

private:
    Packet* markMSS(struct fcb *fcb, Packet *packet);

    int8_t annotation;
    int16_t offset;
    unsigned int flowDirection;

    const uint16_t DEFAULT_MSS = 536; // Minimum MSS for IPV4
};

CLICK_ENDDECLS

#endif
