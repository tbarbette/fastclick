#ifndef MIDDLEBOX_TCPREORDER_HH
#define MIDDLEBOX_TCPREORDER_HH

#include <click/config.h>
#include <click/element.hh>
#include <click/memorypool.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreordernode.hh"
#include "fcb.hh"

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

    void push(int, Packet*);

    // Custom method
    void processPacket(struct fcb *fcb, Packet* packet);

protected:

private:
    void putPacketInList(struct fcb *fcb, Packet* packet);
    void sendEligiblePackets(struct fcb *fcb);
    tcp_seq_t getSequenceNumber(Packet* packet);
    unsigned getPacketLength(Packet* packet);
    void checkFirstPacket(struct fcb *fcb, Packet* packet);
    void flushList(struct fcb *fcb);
    void checkRetransmission(struct fcb *fcb, Packet* packet);

    MemoryPool<struct TCPPacketListNode> pool; // TODO: Ensure that is it per-thread
    unsigned int flowDirection;
};

CLICK_ENDDECLS

#endif
