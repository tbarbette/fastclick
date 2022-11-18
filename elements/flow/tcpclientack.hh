#ifndef MIDDLEBOX_TCPClientAck_HH
#define MIDDLEBOX_TCPClientAck_HH

#include <click/config.h>
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/multithread.hh>
#include "batchfcb.hh"
#include <click/tcphelper.hh>
#include <click/flow/flowelement.hh>

/**
 * Structure used by the TCPClientAck element
 */
struct fcb_clientack
{
    tcp_seq_t last_ack; //Last ack sent
    tcp_seq_t current_seq; //Current seq with data sent
};


CLICK_DECLS

/*
=c

TCPClientAck()

=s ctx

ACKs TCP packet, for the CTX subsystem.

=d

ACKs packets as they are received. It has two outputs, the first one
let the packet pass through and the second one is used to generate the acks.
The second port should be piped to the return side.

 */

class TCPClientAck : public FlowSpaceElement<fcb_clientack>, public TCPHelper
{
public:
    /**
     * @brief Construct a TCPClientAck element
     */
    TCPClientAck() CLICK_COLD;

    /**
     * @brief Destruct a TCPClientAck element
     */
    ~TCPClientAck() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "TCPClientAck"; }
    const char *port_count() const        { return "1/2"; }
    const char *processing() const        { return PUSH; }

    FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff", FLOW_TCP);

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    void push_flow(int, fcb_clientack* fcb, PacketBatch *batch) override;

    /**
     * Set the last data acked while sending packet
     */
    inline void setSeqAcked(tcp_seq_t,tcp_seq_t);

    //static void fcb_release_fnt(FlowControlBlock* fcb, void* thunk);
private:
    int _msec_delayed;

};


inline void TCPClientAck::setSeqAcked(tcp_seq_t acked, tcp_seq_t current) {
    fcb_data()->last_ack = acked;
    fcb_data()->current_seq = current;
}



CLICK_ENDDECLS

#endif
