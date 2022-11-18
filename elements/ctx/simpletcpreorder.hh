#ifndef MIDDLEBOX_STCPREORDER_HH
#define MIDDLEBOX_STCPREORDER_HH

#include <click/config.h>
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/multithread.hh>
#include "../flow/batchfcb.hh"
#include <click/packetbatch.hh>
#include <click/tcphelper.hh>
#include <click/flow/flowelement.hh>

#define TCPREORDER_POOL_SIZE 100


/**
 * Structure used by the SimpleTCPReorder element
 */
struct fcb_simpletcpreorder
{
    Packet* packetList;
    uint16_t packetListLength;
    tcp_seq_t expectedPacketSeq;
    tcp_seq_t lastSent;

    fcb_simpletcpreorder()
    {
        /*packetList = NULL;
        expectedPacketSeq = 0;
        lastSent = 0;
        pool = NULL;*/
    }

    ~fcb_simpletcpreorder()
    {
        //TODO : call this on release
        /*Packet* next = NULL;

        while(packetList != NULL)
        {
            next = packetList->next();

            // Kill packet
            packetList->kill();

            // Put back node in memory pool
            packetList = next;
        }*/
    }
};


CLICK_DECLS

/*
=c

SimpleTCPReorder(FLOWDIRECTION [, MERGESORT])

=s ctx

Reorders TCP packets for the ctx subsystem

=d 

reorders TCP packets. This element is deprecated, the functionality has
been merged in TCPIn. Still, it is interesting to see how to make
a standalone TCP reorderer that will not do much more than that.

Its default is that as it does not speak with the other side of the connection,
a RST is not propagated to the other side. Meaning that if a connection
is reused after a RST, the other side will see packets out of order.
Proper implementation needs a "dual" state, something only accessible after
TCPIn

This element reorders TCP packets before sending them on its first output. It can be used outside
of the stack of the middlebox. The second output is optional and is used to push retransmitted
packets. If the second output is not used, retransmitted packets are dropped.

=item FLOWDIRECTION

ID of the path for the connection (0 or 1). The return path must have the other ID.
Thus, each direction of a TCP connection has a different ID.

=item MERGESORT

Only used with batching. Adds each packet of the batch at the beginning of the list and reorders
the list using merge sort instead of adding each packet of the batch directly to the right position.

Complexity to process a batch with this option: O((n + k) * log (n + k))
Complexity to process a batch without this option: O(k * (n + k))

Where k is the number of packets in the batch and n is the number of packets in the waiting list

Default value: true.

=a TCPIn, TCPOut, TCPRetransmitter */

class SimpleTCPReorder : public FlowSpaceElement<fcb_simpletcpreorder>, public TCPHelper
{
public:
    /**
     * @brief Construct a SimpleTCPReorder element
     */
    SimpleTCPReorder() CLICK_COLD;

    /**
     * @brief Destruct a SimpleTCPReorder element
     */
    ~SimpleTCPReorder() CLICK_COLD;

    const char *class_name() const        { return "SimpleTCPReorder"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PUSH; }


    void* cast(const char *n) override;

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_flow(int, fcb_simpletcpreorder* fcb, PacketBatch *batch) override;

private:
    /**
     * @brief Put a packet in the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to add
     */
    bool putPacketInList(struct fcb_simpletcpreorder *fcb, Packet* packet);

    void killList(struct fcb_simpletcpreorder* tcpreorder);

    /**
     * @brief Send the in-order packets from the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     */
    PacketBatch* sendEligiblePackets(struct fcb_simpletcpreorder *fcb, bool had_awaiting);

    /**
     * @brief Check if the packet is the first one of the flow and acts consequently.
     * In particular, it flushes the list of waiting packets and sets the sequence number of the
     * next expected packet
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to check
     */
    bool checkFirstPacket(struct fcb_simpletcpreorder *fcb, PacketBatch* batch);

    /**
     * @brief Flush the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     */
    void flushList(struct fcb_simpletcpreorder *fcb);

    /**
     * @brief Flush the list of waiting packets after a given packet
     * @param fcb A pointer to the FCB of the flow
     * @param toKeep Pointer to the last element that will be kept in the list
     * @param toRemove Pointer to the first element that will be removed
     */
    void flushListFrom(struct fcb_simpletcpreorder *fcb, Packet *toKeep,
        Packet *toRemove);

   private:

    bool _verbose;
};

CLICK_ENDDECLS

#endif
