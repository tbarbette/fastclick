#ifndef MIDDLEBOX_TCPMARKMSS_HH
#define MIDDLEBOX_TCPMARKMSS_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <click/element.hh>
#include <click/batchelement.hh>
#include "batchfcb.hh"
#include "fcb.hh"
#include "tcphelper.hh"

CLICK_DECLS


/**
 * Structure used by the TCPMarkMSS element
 */
struct fcb_tcpmarkmss
{
    uint16_t mss;

    fcb_tcpmarkmss()
    {
        mss = 0;
    }
};



/*
=c

TCPMarkMSS(FLOWDIRECTION, ANNOTATION [, OFFSET])

=s middlebox

annotates packets with the MSS of the flow

=d

This element detects the MSS of a TCP flow and annotates every packet of the flow with this MSS.
It is typically used in conjunction with TCPFragmenter to ensure that the TCP packets are not
too big.

=item FLOWDIRECTION

ID of the path for the connection (0 or 1). The return path must have the other ID.
Thus, each direction of a TCP connection has a different ID.

=item ANNOTATION

Offset of the annotation

=item OFFSET

Offset to add to the MSS before setting the annotation (can be negative)

=a TCPFragmenter */

class TCPMarkMSS : public BatchElement, public TCPHelper
{
public:
    /**
     * @brief Construct a TCPMarkMSS element
     */
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
    /**
     * @brief Annotate the packet with the MSS of the flow.
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     * @return The packet annotated with the MSS of the flow
     */
    Packet* markMSS(struct fcb *fcb, Packet *packet);

    int8_t annotation;
    int16_t offset;
    unsigned int flowDirection;

    const uint16_t DEFAULT_MSS = 536; // Minimum MSS for IPV4
};

CLICK_ENDDECLS

#endif
