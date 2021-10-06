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




void TCPClientAck::push_flow(int port, fcb_clientack* fcb, PacketBatch *batch)
{
    //assert(batch->count() == batch->first()->find_count());
    Packet* packet = batch->tail();
    auto lastSeq = getSequenceNumber(packet);
    uint32_t saddr = getDestinationAddress(packet);
    uint32_t daddr = getSourceAddress(packet);
    uint16_t sport = getDestinationPort(packet);
    uint16_t dport = getSourcePort(packet);
    uint8_t flags = 0;
    // The SEQ value is the initial ACK value in the packet sent
    // by the source.
    //    if (getAckNumber(packet);

    // The ACK is the sequence number sent by the source
    // to which we add the old size of the payload to acknowledge it
    tcp_seq_t ack = lastSeq + getPayloadLength(packet);
    if (isSyn(packet))
        ack += 1;

    if (isFin(packet)) {
        ack += 1;
        flags = TH_ACK | TH_FIN;
    } else {
        flags = TH_ACK;
    }

    output_push_batch(0,batch);

    //If we did not ACKED while sending data as response to our packets, we must ACK now
    if (lastSeq > fcb->last_ack) {
//        click_chatter("Acking %u",ack);
        // Craft and send the ack
        tcp_seq_t seq = fcb->current_seq;
        WritablePacket* p = forgePacket(saddr, daddr,
            sport, dport, seq, ack, 60*1024, flags);
        output_push_batch(1,PacketBatch::make_from_packet(p));
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPClientAck)
ELEMENT_MT_SAFE(TCPClientAck)
