#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreorder.hh"

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
    .read_p("FLOWDIRECTION", flowDirectionParam)
    .complete() < 0)
        return -1;

    if(flowDirectionParam == -1)
    {
        click_chatter("Missing parameter(s): TCPReorder requires FLOWDIRECTION");
        return -1;
    }

    flowDirection = (unsigned int)flowDirectionParam;

    return 0;
}

void TCPReorder::flushList(fcb_tcpreorder *fcb)
{
    struct TCPPacketListNode* node = fcb->packetList;
    struct TCPPacketListNode* toDelete = NULL;

    while(node != NULL)
    {
        toDelete = node;
        node = node->next;

        // Kill packet
        //toDelete->packet->kill();

        fcb->pool->releaseMemory(toDelete);
    }

    fcb->packetList = NULL;
}

void TCPReorder::push_batch(int port, struct fcb_tcpreorder* fcb, PacketBatch* flow)
{
    FOR_EACH_PACKET_SAFE(flow, packet) {
        // Ensure that the pointer in the FCB is set
        if (fcb->pool == NULL)
            fcb->pool = &pool;

        checkRetransmission(fcb, packet);
        checkFirstPacket(fcb, packet);
        putPacketInList(fcb, packet);
        sendEligiblePackets(fcb);
    }
    output(0).push_batch(flow);
}

void TCPReorder::checkRetransmission(struct fcb_tcpreorder *fcb, Packet* packet)
{
    if(getSequenceNumber(packet) < fcb->expectedPacketSeq)
    {
        if(noutputs() == 2)
            output(1).push(packet);
        else
            packet->kill();
    }
}

void TCPReorder::sendEligiblePackets(struct fcb_tcpreorder *fcb)
{
    struct TCPPacketListNode* packetNode = fcb->packetList;

    while(packetNode != NULL)
    {
        tcp_seq_t currentSeq = getSequenceNumber(packetNode->packet);

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != fcb->expectedPacketSeq)
            return;

        // Compute sequence number of the next packet
        fcb->expectedPacketSeq = currentSeq + getPacketLength(packetNode->packet);
        if(isFinOrSyn(packetNode->packet))
            (fcb->expectedPacketSeq)++;

        // Send packet
        output(0).push(packetNode->packet);

        // Free memory and remove node from the list
        struct TCPPacketListNode* toFree = packetNode;
        packetNode = packetNode->next;
        fcb->packetList = packetNode;
        fcb->pool->releaseMemory(toFree);
    }
}

unsigned TCPReorder::getPacketLength(Packet* packet)
{
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    return ip_len - iph_len - tcp_offset;
}

tcp_seq_t TCPReorder::getSequenceNumber(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
}

void TCPReorder::putPacketInList(struct fcb_tcpreorder* fcb, Packet* packet)
{
    struct TCPPacketListNode* prevNode = NULL;
    struct TCPPacketListNode* packetNode = fcb->packetList;
    struct TCPPacketListNode* toAdd = fcb->pool->getMemory();
    toAdd->packet = packet;
    toAdd->next = NULL;

    // Browse the list until we find a packet with a greater sequence number than the packet to add in the list
    while(packetNode != NULL && (getSequenceNumber(packetNode->packet) < getSequenceNumber(packet)))
    {
        prevNode = packetNode;
        packetNode = packetNode->next;
    }

    // Check if we need to add the node at the head of the list
    if(prevNode == NULL)
        fcb->packetList = toAdd; // If so, the list points to the node to add
    else
        prevNode->next = toAdd; // If not, the previous node in the list now points to the node to add

    toAdd->next = packetNode; // The node to add points to the first node with a greater sequence number
}

void TCPReorder::checkFirstPacket(struct fcb_tcpreorder* fcb, Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if(flags & TH_SYN)
    {
        // Update the expected sequence number
        fcb->expectedPacketSeq = getSequenceNumber(packet);
        click_chatter("First packet received (%u) for flow %u", fcb->expectedPacketSeq, flowDirection);

        // Ensure that the list of waiting packets is free
        // (SYN should always be the first packet)
        flushList(fcb);
    }
}

bool TCPReorder::isFinOrSyn(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if((flags & TH_SYN) || (flags & TH_FIN))
        return true;
    else
        return false;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPReorder)
//ELEMENT_MT_SAFE(TCPReorder)
