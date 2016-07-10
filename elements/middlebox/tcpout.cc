#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpin.hh"
#include "tcpout.hh"
#include "ipelement.hh"

CLICK_DECLS

TCPOut::TCPOut()
{
    inElement = NULL;
}

int TCPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* TCPOut::processPacket(struct fcb* fcb, Packet* p)
{
    if(!checkConnectionClosed(fcb, p))
    {
        p->kill();
        return NULL;
    }

    WritablePacket *packet = p->uniqueify();

    bool hasModificationList = inElement->hasModificationList(fcb, packet);
    ByteStreamMaintainer &byteStreamMaintainer = fcb->tcp_common->maintainers[getFlowDirection()];
    ModificationList *modList = NULL;

    if(hasModificationList)
        modList = inElement->getModificationList(fcb, packet);

    // Update the sequence number (according to modifications made on previous packets)
    tcp_seq_t prevSeq = getSequenceNumber(packet);
    tcp_seq_t newSeq =  byteStreamMaintainer.mapSeq(prevSeq);
    bool seqModified = false;
    bool ackModified = false;
    tcp_seq_t prevAck = getAckNumber(packet);
    tcp_seq_t prevLastAck = byteStreamMaintainer.getLastAckSent();

    if(prevSeq != newSeq)
    {
        click_chatter("Sequence number %u modified to %u in flow %u", prevSeq, newSeq, flowDirection);
        setSequenceNumber(packet, newSeq);
        seqModified = true;
    }

    // Update the last sequence number seen
    // This number is used when crafting ACKs
    byteStreamMaintainer.setLastSeqSent(newSeq);

    // Update the window size
    byteStreamMaintainer.setWindowSize(getWindowSize(packet));

    // Update the value of the last ACK sent
    byteStreamMaintainer.setLastAckSent(prevAck);

    // Ensure that the value of the ACK is not below the last ACKed position
    // This solves the following problem:
    // - We ACK a packet manually for any reason
    // -> The "manual" ACK is lost
    setAckNumber(packet, byteStreamMaintainer.getLastAckSent());

    if(getAckNumber(packet) != prevAck)
        ackModified = true;

    // Check if the packet has been modified
    if(getAnnotationDirty(packet) || seqModified || ackModified)
    {
        // Check the length to see if bytes were added or removed
        uint16_t initialLength = packetTotalLength(packet);
        uint16_t currentLength = (uint16_t)packet->length() - getIPHeaderOffset(packet);
        int offsetModification = -(initialLength - currentLength);
        uint32_t prevPayloadSize = getPayloadLength(packet);

        // Update the "total length" field in the IP header (required to compute the tcp checksum as it is in the pseudo header)
        setPacketTotalLength(packet, initialLength + offsetModification);

        // Check if the modificationlist has to be committed
        if(hasModificationList)
        {
            // We know that the packet has been modified and its size has changed
            modList->commit(fcb->tcp_common->maintainers[getFlowDirection()]);

            // Check if the full packet content has been removed
            if(getPayloadLength(packet) == 0)
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
                sendAck(fcb->tcp_common->maintainers[getOppositeFlowDirection()], saddr, daddr, sport, dport, seq, ack);

                // Even if the packet is empty it can still contain relevant
                // information (significant ACK value or another flag)
                if(isJustAnAck(packet))
                {
                    // Check if the ACK of the packet was significant or not
                    if(SEQ_LEQ(prevAck, prevLastAck))
                    {
                        // If this is not the case, drop the packet as it
                        // does not contain any relevant information
                        // (And anyway it would be considered as a duplicate ACK)
                        click_chatter("Empty packet dropped");
                        packet->kill();
                        return NULL;
                    }
                }
            }
        }

        // Recompute the checksum
        computeTCPChecksum(packet);
    }

    // Notify the stack function that this packet has been sent
    packetSent(fcb, packet);

    return packet;
}

void TCPOut::sendAck(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack)
{
    if(noutputs() < 2)
    {
        click_chatter("Warning: trying to send an ack on a TCPOut with only 1 output");
        return;
    }

    // Check if the ACK does not bring any additional information
    if(SEQ_LEQ(ack, maintainer.getLastAckSent()))
        return;

    // Update the number of the last ack sent for the other side
    maintainer.setLastAckSent(ack);

    // Ensure that the sequence number of the packet is not below
    // a sequence number sent before by the other side
    if(SEQ_LT(seq, maintainer.getLastSeqSent()))
        seq = maintainer.getLastSeqSent();

    uint16_t winSize = maintainer.getWindowSize();

    // The packet is now empty, we discard it and send an ACK directly to the source
    click_chatter("Sending an ACK! (%u)", ack);

    // Craft the packet
    Packet* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, winSize, TH_ACK);

    //Send it on the second output
    output(1).push(forged);
}

void TCPOut::sendClosingPacket(ByteStreamMaintainer &maintainer, uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport, tcp_seq_t seq, tcp_seq_t ack, bool graceful)
{
    if(noutputs() < 2)
    {
        click_chatter("Warning: trying to send an FIN or RST packet on a TCPOut with only 1 output");
        return;
    }

    // Update the number of the last ack sent for the other side
    maintainer.setLastAckSent(ack);

    if(SEQ_LT(seq, maintainer.getLastSeqSent()))
        seq = maintainer.getLastSeqSent();

    uint16_t winSize = maintainer.getWindowSize();

    click_chatter("Sending a closing packet");

    uint8_t flag = TH_ACK;

    if(graceful)
    {
        flag = flag | TH_FIN;
        // Ensure that further packet will have seq + 1 (for FIN flag) as a
        // sequence number
        maintainer.setLastSeqSent(seq + 1);
    }
    else
        flag = flag | TH_RST;

    // Craft the packet
    Packet* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, winSize, flag);

    //Send it on the second output
    output(1).push(forged);
}

void TCPOut::setInElement(TCPIn* inElement)
{
    this->inElement = inElement;
}

bool TCPOut::checkConnectionClosed(struct fcb* fcb, Packet *packet)
{
    if(fcb->tcp_common->closingStates[getFlowDirection()] == TCPClosingState::OPEN)
        return true;

    if(fcb->tcp_common->closingStates[getFlowDirection()] == TCPClosingState::BEING_CLOSED_GRACEFUL)
    {
        if(isFin(packet))
            fcb->tcp_common->closingStates[getFlowDirection()] = TCPClosingState::CLOSED_GRACEFUL;

        return true;
    }
    else if(fcb->tcp_common->closingStates[getFlowDirection()] == TCPClosingState::BEING_CLOSED_UNGRACEFUL)
    {
        if(isRst(packet))
            fcb->tcp_common->closingStates[getFlowDirection()] = TCPClosingState::CLOSED_UNGRACEFUL;

        return true;
    }

    // Otherwise, the connection is closed
    return false;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(ModificationList)
ELEMENT_REQUIRES(TCPElement)
EXPORT_ELEMENT(TCPOut)
//ELEMENT_MT_SAFE(TCPOut)
