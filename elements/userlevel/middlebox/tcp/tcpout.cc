#include <click/config.h>
#include "tcpout.hh"
#include "../ip/ipelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/ip.h>
#include <clicknet/tcp.h>

CLICK_DECLS

TCPOut::TCPOut()
{

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

        if(offsetModification != 0)
        {
            // We know that the packet has been modified and its size has changed
            click_chatter("Packet %u modified, offset: %d", prevSeq, offsetModification);

            // Check if the full packet content has been removed
            if(getPacketLength(packet) + offsetModification == 0)
            {
                // The packet is now empty, we discard it and send an ACK directly to the source
                click_chatter("Empty packet. I am supposed to discard it and send a ACK to the source, but it is still TODO");
            }

            // Notify about the new modifications
            byteStreamMaintainer.newInsertion(newSeq + getPacketLength(packet), offsetModification);

            // Update the "total length" field in the IP header (required to compute the tcp checksum as it is in the pseudo hdr)
            IPElement::setPacketTotalLength(packet, initialLength + offsetModification);
        }

        // Recompute the checksum
        computeChecksum(packet);
    }

    return p;
}

ByteStreamMaintainer* TCPOut::getByteStreamMaintainer()
{
    return &byteStreamMaintainer;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPOut)
//ELEMENT_MT_SAFE(TCPOut)
