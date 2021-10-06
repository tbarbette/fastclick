#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpin.hh"
#include "tcpout.hh"
#include <click/ipelement.hh>

CLICK_DECLS

TCPOut::TCPOut() : inElement(NULL),_readonly(false), _allow_resize(false), _sw_checksum(true)
{

}

int TCPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
        .read_p("READONLY", _readonly)
        .read("CHECKSUM", _sw_checksum)
        .complete() < 0)
            return -1;
    //Set the initialization to run after all stack objects passed
    auto fnt = [this](ErrorHandler* errhin) {
        return tcp_initialize(errhin);
    };
    allStackInitialized.post(new Router::FctFuture(fnt));
    return 0;
}

int
TCPOut::tcp_initialize(ErrorHandler *errh) {
    if (maxModificationLevel(0) & MODIFICATION_RESIZE) {
        click_chatter("%p{element} Flow resizing support enabled",this);
        _allow_resize = true;
        if (_readonly) {
            return errh->error("Cannot modify packets in read-only mode !");
        }
    }
    return 0;
}

void TCPOut::push_batch(int port, PacketBatch* flow)
{
    auto fcb_in = inElement->fcb_data();
    auto fnt = [this,fcb_in](Packet* p) -> Packet* {
        if (_allow_resize) {
            WritablePacket *packet = p->uniqueify();

            if (!fcb_in->common) {
                click_chatter("ERROR : Connection released before all packets did go through. This is an edge case that should not happen");
                packet->kill();
                return NULL;
            }
            fcb_in->common->lock.acquire();
            bool hasModificationList = inElement->hasModificationList(packet);

            ByteStreamMaintainer &byteStreamMaintainer = fcb_in->common->maintainers[getFlowDirection()];
            ModificationList *modList = NULL;

            if(hasModificationList)
                modList = inElement->getModificationList(packet);

            // Update the sequence number (according to the modifications made on previous packets)
            tcp_seq_t prevSeq = getSequenceNumber(packet);
            tcp_seq_t newSeq =  byteStreamMaintainer.mapSeq(prevSeq);
            if (inElement->_verbose)
                click_chatter("Map SEQ %lu -> %lu", prevSeq, newSeq);
            bool seqModified = false;
            bool ackModified = false;
            tcp_seq_t prevAck = getAckNumber(packet);
            tcp_seq_t prevLastAck = 0;
            bool prevLastAckSet = false;

            if(byteStreamMaintainer.isLastAckSentSet())
            {
                prevLastAck = byteStreamMaintainer.getLastAckSent();
                prevLastAckSet = true;
            }

            if(prevSeq != newSeq)
            {
                setSequenceNumber(packet, newSeq);
                seqModified = true;
            }

            // Update the last sequence number seen
            // This number is used when crafting ACKs
            byteStreamMaintainer.setLastSeqSent(newSeq);

            // Update the window size
            byteStreamMaintainer.setWindowSize(getWindowSize(packet));

            // Update the value of the last ACK sent
            if(isAck(packet))
            {
                byteStreamMaintainer.setLastAckSent(prevAck);

                // Ensure that the value of the ACK is not below the last ACKed position
                // This solves the following problem:
                // - We ACK a packet manually for any reason
                // -> The "manual" ACK is lost
                //click_chatter("Ack %lu -> %lu", prevAck,  byteStreamMaintainer.getLastAckSent());
                setAckNumber(packet, byteStreamMaintainer.getLastAckSent());

                if(getAckNumber(packet) != prevAck)
                    ackModified = true;
            }

            // Check the length to see if bytes were added or removed
            uint16_t initialLength = packetTotalLength(packet);
            uint16_t currentLength = (uint16_t)packet->length() - getIPHeaderOffset(packet);
            int offsetModification = -(initialLength - currentLength);
            uint32_t prevPayloadSize = getPayloadLength(packet);

            // Update the "total length" field in the IP header (required to compute the
            // tcp checksum as it is in the pseudo header)
            setPacketTotalLength(packet, initialLength + offsetModification);
            byteStreamMaintainer.setLastPayloadLength(prevPayloadSize + offsetModification);

            // Check if the ModificationList has to be committed
            if(hasModificationList)
            {
                // We know that the packet has been modified and its size has changed
                modList->commit(fcb_in->common->maintainers[getFlowDirection()]);

                // Check if the full content of the packet has been removed
                if(getPayloadLength(packet) == 0) //Some rightfull ack get here TODO
                {
                    uint32_t saddr = getDestinationAddress(packet);
                    uint32_t daddr = getSourceAddress(packet);
                    uint16_t sport = getDestinationPort(packet);
                    uint16_t dport = getSourcePort(packet);
                    // The SEQ value is the initial ACK value in the packet sent
                    // by the source.
                    tcp_seq_t seq = getInitialAck(packet);

                    // The ACK is the sequence number sent by the source
                    // to which we add the old size of the payload to acknowledge it
                    tcp_seq_t ack = prevSeq + prevPayloadSize;

                    if(isFin(packet) || isSyn(packet))
                        ack++;

                    // Craft and send the ack
                    Packet* forged = forgeAck(fcb_in->common->maintainers[getOppositeFlowDirection()], saddr, daddr,
                        sport, dport, seq, ack);

                    fcb_in->common->lock.release();
                    if (forged)
                        sendOpposite(forged);


                    // Even if the packet is empty it can still contain relevant
                    // information (significant ACK value or another flag)
                    if(isJustAnAck(packet))
                    {
                        // Check if the ACK of the packet was significant or not
                        if(prevLastAckSet && SEQ_LEQ(prevAck, prevLastAck))
                        {
                            // If this is not the case, drop the packet as it
                            // does not contain any relevant information
                            // (And anyway it would be considered as a duplicate ACK)
                            click_chatter("Killing useless ACK");
                            packet->kill();
                            return NULL;
                        }
                    }
                } else

                    fcb_in->common->lock.release();
            } else
                fcb_in->common->lock.release();

/*                if(prevLastAckSet && SEQ_LEQ(prevAck, prevLastAck)) {
                            click_chatter("ACK lower than already sent");
                }*/


            if (!_sw_checksum)
                resetTCPChecksum(packet);
            else
                computeTCPChecksum(packet);
            return packet;
        } else {
            tcp_seq_t seq = getSequenceNumber(p);
            tcp_seq_t ack = getAckNumber(p);
            uint16_t winSize = getWindowSize(p);
            //No need to lock, this is our side
            //fcb_in->common->lock.acquire();

            // Update the last sequence number seen
            // This number is used when crafting ACKs
            ByteStreamMaintainer &byteStreamMaintainer = fcb_in->common->maintainers[getFlowDirection()];
            byteStreamMaintainer.setLastSeqSent(seq);
            byteStreamMaintainer.setLastPayloadLength(getPayloadLength(p));

            // Update the window size
            byteStreamMaintainer.setWindowSize(winSize);

            // Update the value of the last ACK sent
            if(isAck(p))
            {
                byteStreamMaintainer.setLastAckSent(ack);
            }
            //fcb_in->common->lock.release();

            if (!_readonly) {
                // Recompute the checksum
                WritablePacket *packet = p->uniqueify();
                p = packet;
                if (!_sw_checksum)
                    resetTCPChecksum(packet);
                else
                    computeTCPChecksum(packet);
            }
            return p;
        }
    };
    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, flow, [](Packet*){});

    output(0).push_batch(flow);
}

Packet*
TCPOut::forgeAck(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr,
    uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, bool force)
{
    //click_chatter("Gen ack");
    if(noutputs() < 2)
    {
        click_chatter("Warning: trying to send an ack on a TCPOut with only 1 output. How could I send an ACK to the source ?");
        return 0;
    }


    // Check if the ACK does not bring any additional information
    if(!force && maintainer.isLastAckSentSet() && SEQ_LEQ(ack, maintainer.getLastAckSent())) {
        if (inElement->_verbose)
            click_chatter("Ack not sent, no new knowledge");
        return 0;
    }

    // Update the number of the last ack sent for the other side
    maintainer.setLastAckSent(ack);

    // Ensure that the sequence number of the packet is not below
    // a sequence number sent before by the other side
    if(maintainer.isLastSeqSentSet() && SEQ_LT(seq, maintainer.getLastSeqSent()))
        seq = maintainer.getLastSeqSent();

    uint16_t winSize = maintainer.getWindowSize();

    // The packet is now empty, we discard it and send an ACK directly to the source

    // Craft the packet
    Packet* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, winSize, TH_ACK);

    return forged;
}

void TCPOut::sendOpposite(Packet* p) {
    PacketBatch *batch =  PacketBatch::make_from_packet(p);
    output_push_batch(1, batch);
}

void TCPOut::sendClosingPacket(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr,
    uint16_t sport, uint16_t dport, int graceful)
{
    /*if(noutputs() < 2)
    {
        click_chatter("Warning: trying to send a FIN or RST packet on a TCPOut with only 1 output");
        return;
    }*/

    // Update the number of the last ack sent for the other side
    //maintainer.setLastAckSent(ack);


    tcp_seq_t seq, ack;
    if(maintainer.isLastSeqSentSet() && maintainer.isLastAckSentSet()) {
        seq = maintainer.getLastSeqSent() + maintainer.getLastPayloadLength();
        ack = maintainer.getLastAckSent();
    } else {
        click_chatter("Cannot close a connection that never had a packet out");
        return;
    }

    uint16_t winSize = maintainer.getWindowSize();

    uint8_t flag = TH_ACK;

    if(graceful == 1)
    {
        flag = flag | TH_FIN;
        // Ensure that further packets will have seq + 1 (for the FIN flag) as a
        // sequence number
        maintainer.setLastSeqSent(seq + 1);
        maintainer.setLastPayloadLength(0);
    }
    else if (graceful == 0) {
        flag = flag | TH_RST;
    } else {
        click_chatter("Unknown graceful, flag not changed");
    }

    // Craft the packet
    Packet* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, winSize, flag);

    //Send it on the second output
    PacketBatch *batch =  PacketBatch::make_from_packet(forged);
    output_push_batch(0, batch);
}

int TCPOut::setInElement(TCPIn* inElement, ErrorHandler* errh)
{
    this->inElement = inElement;
    inElement->add_remote_element(this);
    return 0;
}

bool TCPOut::checkConnectionClosed(Packet *packet)
{
    auto fcb_in = inElement->fcb_data();

    TCPState::Value state = fcb_in->common->state; //Read-only for fast path

    return state == TCPState::CLOSED;

}

void TCPOut::setFlowDirection(unsigned int flowDirection)
{
    this->flowDirection = flowDirection;
}

unsigned int TCPOut::getFlowDirection()
{
    return flowDirection;
}

unsigned int TCPOut::getOppositeFlowDirection()
{
    return (1 - flowDirection);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPOut)
ELEMENT_MT_SAFE(TCPOut)
