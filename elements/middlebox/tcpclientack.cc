#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpclientack.hh"

CLICK_DECLS

TCPClientAck::TCPClientAck() : _msec_delayed(0)
{
}

TCPClientAck::~TCPClientAck()
{
}

int TCPClientAck::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
            .read_p("DELAYED",_msec_delayed)
            .complete() < 0)
        return -1;

    if (_msec_delayed != 0) {
        errh->error("Unimplemented yet");
    }
    return 0;
}




void TCPClientAck::push_batch(int port, fcb_clientack* fcb, PacketBatch *batch)
{
    Packet* packet = batch->tail();
    auto lastSeq = getSequenceNumber(packet);
    if (lastSeq > fcb->last_seq) {
        uint32_t saddr = getDestinationAddress(packet);
        uint32_t daddr = getSourceAddress(packet);
        uint16_t sport = getDestinationPort(packet);
        uint16_t dport = getSourcePort(packet);
        // The SEQ value is the initial ACK value in the packet sent
        // by the source.
        tcp_seq_t seq = getAckNumber(packet);

        // The ACK is the sequence number sent by the source
        // to which we add the old size of the payload to acknowledge it
        tcp_seq_t ack = lastSeq;

        // Craft and send the ack
        WritablePacket* p = forgePacket(saddr, daddr,
            sport, dport, seq, ack, 60*1024, TH_ACK);
        output_push_batch(1,PacketBatch::make_from_packet(p));
    }
    output_push_batch(0,batch);
}



CLICK_ENDDECLS
EXPORT_ELEMENT(TCPClientAck)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_MT_SAFE(TCPClientAck)
