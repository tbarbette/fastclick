#ifndef MIDDLEBOX_TCPREORDER_HH
#define MIDDLEBOX_TCPREORDER_HH

#include <click/config.h>
#include <click/flowelement.hh>
#include <click/memorypool.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreordernode.hh"

#define TCPREORDER_POOL_SIZE 20

CLICK_DECLS


class fcb_tcpreorder
{
public:
    struct TCPPacketListNode* packetList;
    tcp_seq_t expectedPacketSeq;
    MemoryPool<struct TCPPacketListNode> *pool;

    fcb_tcpreorder()
    {
        packetList = NULL;
        expectedPacketSeq = 0;
        pool = NULL;
    }

    ~fcb_tcpreorder()
    {
        // Clean the list and free memory
        struct TCPPacketListNode* node = packetList;
        struct TCPPacketListNode* toDelete = NULL;

        while(node != NULL)
        {
            toDelete = node;
            node = node->next;

            // Kill packet
            toDelete->packet->kill();

            // Put back node in memory pool
            pool->releaseMemory(toDelete);
        }
        packetList = NULL;
    }
};

class TCPReorder : public FlowBufferElement<fcb_tcpreorder>
{
public:
    TCPReorder() CLICK_COLD;
    ~TCPReorder();

    // Click related methods
    const char *class_name() const        { return "TCPReorder"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    void push_batch(int port, fcb_tcpreorder* flowdata, PacketBatch* flow) override;

protected:

private:
    void putPacketInList(fcb_tcpreorder *fcb, Packet* packet);
    void sendEligiblePackets(fcb_tcpreorder *fcb);
    tcp_seq_t getSequenceNumber(Packet* packet);
    unsigned getPacketLength(Packet* packet);
    void checkFirstPacket(fcb_tcpreorder *fcb, Packet* packet);
    bool isFinOrSyn(Packet* packet);
    void flushList(fcb_tcpreorder *fcb);
    void checkRetransmission(fcb_tcpreorder *fcb, Packet* packet);

    MemoryPool<struct TCPPacketListNode> pool; // TODO: Ensure that is it per-thread
    unsigned int flowDirection;
};

CLICK_ENDDECLS

#endif
