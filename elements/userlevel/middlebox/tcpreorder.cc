#include <click/config.h>
#include "tcpreorder.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>

CLICK_DECLS

TCPReorder::TCPReorder() : pool(TCPREORDER_POOL_SIZE)
{
    expectedPacketSeq = 0;
    packetList = NULL;
}

TCPReorder::~TCPReorder()
{
    flushList();
}

void TCPReorder::flushList()
{
    struct TCPPacketListNode* node = packetList;
    struct TCPPacketListNode* toDelete = NULL;

    while(node != NULL)
    {
        toDelete = node;
        node = node->next;

        pool.releaseMemory(toDelete);
    }

    packetList = NULL;
}

int TCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void TCPReorder::push(int, Packet *packet)
{
    processPacket(packet);
}

void TCPReorder::processPacket(Packet* packet)
{
    checkRetransmission(packet);
    checkFirstPacket(packet);
    putPacketInList(packet);
    sendEligiblePackets();
}

void TCPReorder::checkRetransmission(Packet* packet)
{
    if(getSequenceNumber(packet) < expectedPacketSeq)
    {
        if(noutputs() == 2)
            output(1).push(packet);
        else
            packet->kill();
    }
}

void TCPReorder::sendEligiblePackets()
{
    struct TCPPacketListNode* packetNode = packetList;

    while(packetNode != NULL)
    {
        tcp_seq_t currentSeq = getSequenceNumber(packetNode->packet);

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != expectedPacketSeq)
            return;

        // Compute sequence number of the next packet
        expectedPacketSeq = currentSeq + getPacketLength(packetNode->packet);
        if(isFinOrSyn(packetNode->packet))
            expectedPacketSeq++;

        // Send packet
        output(0).push(packetNode->packet);

        // Free memory and remove node from the list
        struct TCPPacketListNode* toFree = packetNode;
        packetNode = packetNode->next;
        packetList = packetNode;
        pool.releaseMemory(toFree);
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

void TCPReorder::putPacketInList(Packet* packet)
{
    struct TCPPacketListNode* prevNode = NULL;
    struct TCPPacketListNode* packetNode = packetList;
    struct TCPPacketListNode* toAdd = pool.getMemory();
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
        packetList = toAdd; // If so, the list points to the node to add
    else
        prevNode->next = toAdd; // If not, the previous node in the list now points to the node to add

    toAdd->next = packetNode; // The node to add points to the first node with a greater sequence number
}

void TCPReorder::checkFirstPacket(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if(flags & TH_SYN)
    {
        // Update the expected sequence number
        expectedPacketSeq = getSequenceNumber(packet);
        click_chatter("First packet received (%u)", expectedPacketSeq);

        // Delete all packets that could be in list as they are probably artifacts
        // (SYN should always be the first packet)
        flushList();
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
