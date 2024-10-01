    #ifndef MIDDLEBOX_SimpleTCPRetransmitter_HH
#define MIDDLEBOX_SimpleTCPRetransmitter_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/timer.hh>
#include <click/timestamp.hh>
#include <click/sync.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <clicknet/tcp.h>
#include <click/tcphelper.hh>
#include <click/flow/flowelement.hh>
#include "tcpin.hh"

CLICK_DECLS

struct fcb_transmit_buffer {
    PacketBatch* first_unacked;
    tcp_seq_t first_unacked_seq;
};

/*
=c

SimpleTCPRetransmitter([INITIALBUFFERSIZE])

=s ctx

Retransmit TCP packets for the ctx subsystem

=d

TCPRetransmitter to use with TCPIn. Keeps reference of packets pushed
on port 0 in the middleclick sractchpad until the ack is received on the other
side. If a packet is sent on port 1, it will retransmit the packet kept in
memory matching the sequence number.

This elements therfore allow to protect against sequence overwriting attacks but
completely lets both sides manage the retransmission and timings. If
you don't care, then simply push the retransmissions directly, without this
element (eg fine for a NAT, LB, ... packet besed fct).

=a TCPIn, TCPOut, TCPReorder, TCPRetransmitter */



class SimpleTCPRetransmitter : public CTXStateElement<SimpleTCPRetransmitter,fcb_transmit_buffer>, public TCPHelper
{
public:
    /**
     * @brief Construct a SimpleTCPRetransmitter element
     */
    SimpleTCPRetransmitter() CLICK_COLD;

    /**
     * @brief Destruct a SimpleTCPRetransmitter element
     */
    ~SimpleTCPRetransmitter() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "SimpleTCPRetransmitter"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }
    void *cast(const char *);

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int retransmitter_initialize(ErrorHandler *) CLICK_COLD;

    void push_flow(int port, fcb_transmit_buffer*, PacketBatch *batch);

    static const int timeout = 0; //Timeout will be managed by TCP

    void forward_packets(fcb_transmit_buffer* fcb, PacketBatch* batch);
    void release_flow(fcb_transmit_buffer*);


    virtual void addCTXElementInList(CTXElement* element, int port) {
        if (port != 0)
            return;
        CTXElement::addCTXElementInList(element,port);
    }
protected:

    /**
     * @brief Prune the buffer. Used when data are ACKed by the destination
     * @param fcb A pointer to the FCB of the flow
     */
    void prune(fcb_transmit_buffer* fcb);


    /**
     * @brief Process a retransmitted packet (coming on the second input)
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to process
     * @return A pointer to the processed packet
     */
    Packet* processPacketRetransmission(Packet *packet);

    bool _verbose;
    bool _proack;
    TCPIn* _in;
    bool _resize;
    bool _readonly;
};

CLICK_ENDDECLS

#endif
