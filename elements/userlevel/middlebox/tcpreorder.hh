#ifndef MIDDLEBOX_TCPREORDER_HH
#define MIDDLEBOX_TCPREORDER_HH

#include <click/config.h>
#include <click/element.hh>
#include <click/memorypool.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>

#define TCPREORDER_POOL_SIZE 20

CLICK_DECLS

class TCPReorder : public Element
{
public:
    TCPReorder() CLICK_COLD;
    ~TCPReorder();

    // Click related methods
    const char *class_name() const        { return "TCPReorder"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    void push(int, Packet *);

    // Custom method
    void processPacket(Packet*);

protected:

private:
    void putPacketInList(Packet*);
    void sendEligiblePackets();
    tcp_seq_t getSequenceNumber(Packet*);
    unsigned getPacketLength(Packet*);
    void checkFirstPacket(Packet*);
    bool isFinOrSyn(Packet*);
    void flushList();
    void checkRetransmission(Packet*);

    struct TCPPacketListNode* packetList;
    MemoryPool<struct TCPPacketListNode> pool;
    tcp_seq_t expectedPacketSeq;
};

struct TCPPacketListNode
{
    Packet* packet;
    struct TCPPacketListNode* next;
};

CLICK_ENDDECLS

#endif
