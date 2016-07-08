#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreorder.hh"
#include "tcpelement.hh"

CLICK_DECLS

TCPReorder::TCPReorder() : pool(TCPREORDER_POOL_SIZE)
{

}

TCPReorder::~TCPReorder()
{
}

int TCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
    .read_mp("FLOWDIRECTION", flowDirectionParam)
    .complete() < 0)
        return -1;

    flowDirection = (unsigned int)flowDirectionParam;

    return 0;
}

void TCPReorder::flushList(struct fcb *fcb)
{
    struct TCPPacketListNode* head = fcb->tcpreorder.packetList;

    // Flush the entire list
    flushListFrom(fcb, NULL, head);
}

void TCPReorder::flushListFrom(struct fcb *fcb, struct TCPPacketListNode *toKeep, struct TCPPacketListNode *toRemove)
{
    // toKeep will be the last packet in the list
    if(toKeep != NULL)
        toKeep->next = NULL;

    if(toRemove == NULL)
        return;

    // Update the head if the list is going to be empty
    if(fcb->tcpreorder.packetList == toRemove)
    {
        fcb->tcpreorder.packetList = NULL;
    }

    struct TCPPacketListNode* toFree = NULL;

    while(toRemove != NULL)
    {
        toFree = toRemove;
        toRemove = toRemove->next;

        // Kill packet
        if(toFree->packet != NULL)
            toFree->packet->kill();

        fcb->tcpreorder.pool->releaseMemory(toFree);
    }
}

void TCPReorder::push(int, Packet *packet)
{
    processPacket(&fcbArray[flowDirection], packet);
}

void TCPReorder::processPacket(struct fcb *fcb, Packet* packet)
{
    // Ensure that the pointer in the FCB is set
    if(fcb->tcpreorder.pool == NULL)
        fcb->tcpreorder.pool = &pool;

    checkFirstPacket(fcb, packet);
    if(!checkRetransmission(fcb, packet))
        return;
    putPacketInList(fcb, packet);
    sendEligiblePackets(fcb);
}

bool TCPReorder::checkRetransmission(struct fcb *fcb, Packet* packet)
{
    if(SEQ_LT(getSequenceNumber(packet), fcb->tcpreorder.expectedPacketSeq))
    {
        if(noutputs() == 2)
            output(1).push(packet);
        else
            packet->kill();
        return false;
    }

    return true;
}

void TCPReorder::sendEligiblePackets(struct fcb *fcb)
{
    struct TCPPacketListNode* packetNode = fcb->tcpreorder.packetList;

    while(packetNode != NULL)
    {
        tcp_seq_t currentSeq = getSequenceNumber(packetNode->packet);

        // Check if the previous packet overlaps with the current one
        // (the expected sequence number is greater than the one of the packet
        // meaning that the new packet shares the begin of its content with
        // the end of the previous packet)
        // This case occurs when there was a gap in the list because a packet
        // had been lost, and the source retransmits the packets with the
        // content split differently.
        // Thus, the previous packets that were after the gap are not
        // correctly align and must be dropped as the source will retransmit
        // them with the new alignment.
        if(SEQ_LT(currentSeq, fcb->tcpreorder.expectedPacketSeq))
        {
            click_chatter("Warning: received a retransmission with a different split");
            flushListFrom(fcb, NULL, packetNode);
            return;
        }

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != fcb->tcpreorder.expectedPacketSeq)
            return;

        // Compute sequence number of the next packet
        fcb->tcpreorder.expectedPacketSeq = getNextSequenceNumber(packetNode->packet);

        // Send packet
        output(0).push(packetNode->packet);

        // Free memory and remove node from the list
        struct TCPPacketListNode* toFree = packetNode;
        packetNode = packetNode->next;
        fcb->tcpreorder.packetList = packetNode;
        fcb->tcpreorder.pool->releaseMemory(toFree);
    }
}

tcp_seq_t TCPReorder::getSequenceNumber(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
}

tcp_seq_t TCPReorder::getNextSequenceNumber(Packet* packet)
{
    tcp_seq_t currentSeq = getSequenceNumber(packet);

    tcp_seq_t nextSeq = currentSeq + getPayloadLength(packet);

    if(isFin(packet) || isSyn(packet))
        nextSeq++;

    return nextSeq;
}

void TCPReorder::putPacketInList(struct fcb* fcb, Packet* packet)
{
    struct TCPPacketListNode* prevNode = NULL;
    struct TCPPacketListNode* packetNode = fcb->tcpreorder.packetList;
    struct TCPPacketListNode* toAdd = fcb->tcpreorder.pool->getMemory();
    toAdd->packet = packet;
    toAdd->next = NULL;

    // Browse the list until we find a packet with a greater sequence number than the packet to add in the list
    while(packetNode != NULL && (SEQ_LT(getSequenceNumber(packetNode->packet), getSequenceNumber(packet))))
    {
        prevNode = packetNode;
        packetNode = packetNode->next;
    }

    // Check if we need to add the node at the head of the list
    if(prevNode == NULL)
        fcb->tcpreorder.packetList = toAdd; // If so, the list points to the node to add
    else
        prevNode->next = toAdd; // If not, the previous node in the list now points to the node to add

    toAdd->next = packetNode; // The node to add points to the first node with a greater sequence number
}

void TCPReorder::checkFirstPacket(struct fcb* fcb, Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if(flags & TH_SYN)
    {
        // Update the expected sequence number
        fcb->tcpreorder.expectedPacketSeq = getSequenceNumber(packet);
        click_chatter("First packet received (%u) for flow %u", fcb->tcpreorder.expectedPacketSeq, flowDirection);

        // Ensure that the list of waiting packets is free
        // (SYN should always be the first packet)
        flushList(fcb);
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPReorder)
ELEMENT_REQUIRES(TCPElement)
//ELEMENT_MT_SAFE(TCPReorder)
