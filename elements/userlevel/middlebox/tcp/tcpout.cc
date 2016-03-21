#include <click/config.h>
#include "tcpout.hh"
#include "../ip/ipelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include "tcpin.hh"

CLICK_DECLS

TCPOut::TCPOut()
{
    inElement = NULL;
}

int TCPOut::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

Packet* TCPOut::processPacket(Packet* p)
{
    WritablePacket *packet = p->uniqueify();

    // Update the sequence number (according to modifications made on previous packets)
    tcp_seq_t prevSeq = getSequenceNumber(packet);
    tcp_seq_t newSeq =  prevSeq + byteStreamMaintainer.getSeqOffset();
    bool seqModified = false;

    if(prevSeq != newSeq)
    {
        setSequenceNumber(packet, newSeq);
        seqModified = true;
    }

    // Check if the packet has been modified
    if(getAnnotationModification(packet) || seqModified)
    {
        // Check the length to see if bytes were added or removed
        uint16_t initialLength = IPElement::packetTotalLength(packet);
        uint16_t currentLength = (uint16_t)packet->length();
        int offsetModification = -(initialLength - currentLength);

        // Test to remove whole packet
        if(offsetModification != 0)
            offsetModification = -getPayloadLength(packet);

        if(offsetModification != 0)
        {
            // We know that the packet has been modified and its size has changed

            // Notify about the new modifications
            byteStreamMaintainer.newInsertion(newSeq + getPayloadLength(packet), offsetModification);

            // Check if the full packet content has been removed
            if(getPayloadLength(packet) + offsetModification == 0)
            {
                uint32_t saddr = IPElement::getDestinationAddress(packet);
                uint32_t daddr = IPElement::getSourceAddress(packet);
                uint16_t sport = getDestinationPort(packet);
                uint16_t dport = getSourcePort(packet);
                tcp_seq_t seq = getAckNumber(packet);
                tcp_seq_t ack = prevSeq + getPayloadLength(packet);

                // The packet is now empty, we discard it and send an ACK directly to the source
                click_chatter("Empty packet. I send an ACK! (%u - %u - %d)", saddr, daddr, offsetModification);

                Packet* forged = forgeAck(saddr, daddr, sport, dport, seq, ack);
                packet->kill();

                if(forged == NULL)
                    click_chatter("Unable to forge packet!");

                // Send it
                inElement->getReturnElement()->getOutElement()->push(0, forged);

                return NULL;
            }

            // Update the "total length" field in the IP header (required to compute the tcp checksum as it is in the pseudo hdr)
            IPElement::setPacketTotalLength(packet, initialLength + offsetModification);
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

ByteStreamMaintainer* TCPOut::getByteStreamMaintainer()
{
    return &byteStreamMaintainer;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPOut)
//ELEMENT_MT_SAFE(TCPOut)
