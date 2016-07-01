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
    WritablePacket *packet = p->uniqueify();

    ByteStreamMaintainer &byteStreamMaintainer = fcb->tcp_common->maintainers[getFlowDirection()];
    bool hasModificationList = inElement->hasModificationList(fcb, packet);
    ModificationList *modList = NULL;

    if(hasModificationList)
        modList = inElement->getModificationList(fcb, packet);

    // Update the sequence number (according to modifications made on previous packets)
    tcp_seq_t prevSeq = getSequenceNumber(packet);
    tcp_seq_t newSeq =  byteStreamMaintainer.mapSeq(prevSeq);
    bool seqModified = false;

    if(prevSeq != newSeq)
    {
        click_chatter("Sequence number %u modified to %u in flow %u", prevSeq, newSeq, flowDirection);
        setSequenceNumber(packet, newSeq);
        seqModified = true;
    }

    // Check if the packet has been modified
    if(getAnnotationModification(packet) || seqModified)
    {
        // Check the length to see if bytes were added or removed
        uint16_t initialLength = IPElement::packetTotalLength(packet);
        uint16_t currentLength = (uint16_t)packet->length() - IPElement::getIPHeaderOffset(packet);
        int offsetModification = -(initialLength - currentLength);

        // Update the "total length" field in the IP header (required to compute the tcp checksum as it is in the pseudo hdr)
        IPElement::setPacketTotalLength(packet, initialLength + offsetModification);

        // Check if the modificationlist has to be committed
        if(hasModificationList)
        {
            // We know that the packet has been modified and its size has changed
            modList->commit(fcb->tcp_common->maintainers[getFlowDirection()]);

            // TODO: Rework that part
            // Check if the full packet content has been removed
            if(getPayloadLength(packet) == 0)
            {
                uint32_t saddr = IPElement::getDestinationAddress(packet);
                uint32_t daddr = IPElement::getSourceAddress(packet);
                uint16_t sport = getDestinationPort(packet);
                uint16_t dport = getSourcePort(packet);
                tcp_seq_t seq = getAckNumber(packet);
                uint8_t winSize = getWindowSize(packet);
                tcp_seq_t ack = prevSeq + getPayloadLength(packet);

                // The packet is now empty, we discard it and send an ACK directly to the source
                click_chatter("Empty packet. Sending an ACK! (%u - %u - %d)", saddr, daddr, offsetModification);

                Packet* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, winSize, TH_ACK);
                packet->kill();

                // Send it
                inElement->getReturnElement()->getOutElement()->push(0, forged);

                return NULL;
            }
        }

        // Recompute the checksum
        computeChecksum(packet);
    }

    return packet;
}

void TCPOut::setInElement(TCPIn* inElement)
{
    this->inElement = inElement;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPOut)
//ELEMENT_MT_SAFE(TCPOut)
