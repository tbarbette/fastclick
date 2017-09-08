/*
 * SimpleTCPRetransmitter.{cc,hh}
 *
 * Tom Barbette
 *
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include "simpletcpretransmitter.hh"


SimpleTCPRetransmitter::SimpleTCPRetransmitter()
{

}

SimpleTCPRetransmitter::~SimpleTCPRetransmitter()
{

}

int SimpleTCPRetransmitter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .complete() < 0)
        return -1;

    ElementCastTracker visitor(router(), "TCPIn");
    router()->visit_upstream(this,0,&visitor);
    if (visitor.size() != 1) {
        return errh->error("Could not find TCPIn element !");
    } else {
        _in = static_cast<TCPIn*>(visitor[0]);
    }
    return 0;
}


int SimpleTCPRetransmitter::initialize(ErrorHandler *errh) {
    if (FlowStateElement<SimpleTCPRetransmitter,fcb_transmit_buffer>::initialize(errh) != 0)
        return -1;
    if (_in->allow_resize()) {
        return errh->error("SimpleTCPRetransmitter does not work when resizing the flow ! Use the non simple one.");
    }
    return 0;
}

void
SimpleTCPRetransmitter::release_flow(fcb_transmit_buffer* fcb) {
    fcb->first_unacked->fast_kill();
    return;
}

void SimpleTCPRetransmitter::push_batch(int port,fcb_transmit_buffer* fcb, PacketBatch *batch)
{
    if(port == 0) {
        /**
         * Just prune the buffer and put this packet in the list.
         */
        prune(fcb);
        fcb->first_unacked->append_batch(batch->clone_batch());
    } else {
        /**
         * The retransmission happens because the ack was never received.
         * There are two cases :
         * - The ack was sent by the dest but did not achive the source, we may
         *   have it in which case we drop the packet and re-ack.
         * - This packet never reached the end host. We retransmit the
         * equivalent packet in the buffer. We cannot retransmit the same packet
         * because it could be a retransmit attack.
         */
        prune(fcb);
        auto fcb_in = _in->fcb_data();
        Packet* lastretransmit = 0;
        FOR_EACH_PACKET_SAFE(batch, packet) {
            uint32_t seq = getSequenceNumber(packet);
            if (SEQ_LT(seq, fcb_in->common->getLastAckReceived(_in->getOppositeFlowDirection()))) {
                click_chatter("Client just did not receive the ack, let's ACK him (seq %lu, last ack %lu)",seq,fcb_in->common->getLastAckReceived(_in->getOppositeFlowDirection()));
                _in->ackPacket(packet,true);
                continue;
            }
            //Seq is bigger than last ack, (the original of) this packet was lost before the dest reached it (or we never received the ack)
            FOR_EACH_PACKET(fcb->first_unacked, pr) {
                if (getSequenceNumber(pr) == seq) {
                    if (lastretransmit == pr) { //Avoid double retransmission
                    } else {
                        lastretransmit = pr;
                        checked_output_push_batch(0,PacketBatch::make_from_packet(pr->clone()));
                    }
                    goto found;
                }
            }
            click_chatter("ERROR : Received a retransmit for a packet not in the buffer (%lu)", seq);
            found:
            continue;
        }
        batch->fast_kill();

    }

    if(batch != NULL)
        output_push_batch(0, batch);
}




void SimpleTCPRetransmitter::prune(fcb_transmit_buffer* fcb)
{
    tcp_seq_t seq = _in->fcb_data()->common->getLastAckReceived(_in->getOppositeFlowDirection());
    Packet* next = fcb->first_unacked;
    Packet* last = 0;
    int count = 0;
    while (next && SEQ_LEQ(getSequenceNumber(fcb->first_unacked),seq)) {
        last = next;
        count++;
    }
    if (count) {
        PacketBatch* second;
        fcb->first_unacked->cut(last, count, second);
        fcb->first_unacked->fast_kill();
        fcb->first_unacked = second;
    }
}


ELEMENT_REQUIRES(TCPElement)
EXPORT_ELEMENT(SimpleTCPRetransmitter)
ELEMENT_MT_SAFE(SimpleTCPRetransmitter)
