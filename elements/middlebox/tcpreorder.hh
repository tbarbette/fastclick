#ifndef MIDDLEBOX_TCPREORDER_HH
#define MIDDLEBOX_TCPREORDER_HH

#include <click/config.h>
#include <click/element.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include "batchfcb.hh"
#include "tcpreordernode.hh"
#include "memorypool.hh"
#include "fcb.hh"
#include "tcpelement.hh"

#define TCPREORDER_POOL_SIZE 20

CLICK_DECLS

/*
=c

TCPReorder(FLOWDIRECTION [, MERGESORT])

=s middlebox

reorder TCP packets

=d

This element reorder TCP packets before sending them on its first output. It can be used outside
of the stack of the middlebox. The second output is optional and is used to push retransmitted
packets. If the second output is not used, retransmitted packets are dropped.

=item FLOWDIRECTION

ID of the path for the connection (0 or 1). The return path must have the other ID.
Thus, each direction of a TCP connection has a different ID.

=item MERGESORT

Only used with batching. Add each packets of the batch at the beginning of the list and reorder
the list using merge sort instead of adding each packet of the batch directly at the right position.

Complexity to process a batch with this option: O((n + k) * log (n + k))
Complexity to process a batch without this option: O(k * n)

Where k is the number of packets in the batch and n is the number of packets in the waiting list

Default value: true.

=a TCPIn, TCPOut, TCPRetransmitter */

class TCPReorder : public BatchElement, public TCPElement
{
public:
    /**
     * @brief Construct a TCPReorder element
     */
    TCPReorder() CLICK_COLD;

    /**
     * @brief Destruct a TCPReorder element
     */
    ~TCPReorder() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "TCPReorder"; }
    const char *port_count() const        { return PORTS_1_1X2; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    void push_packet(int, Packet*);

    #if HAVE_BATCH
    void push_batch(int, PacketBatch *batch);
    #endif

private:
    /**
     * @brief Process a packet (ensure that it is in order)
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     */
    void processPacket(struct fcb *fcb, Packet* packet);

    /**
     * @brief Process a batch of packets (ensure that they are in order)
     * @param fcb A pointer to the FCB of the flow
     * @param batch The batch of packets
     */
    void processPacketBatch(struct fcb *fcb, PacketBatch* batch);

    /**
     * @brief Put a packet in the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to add
     */
    void putPacketInList(struct fcb *fcb, Packet* packet);

    /**
     * @brief Send the set of in order packets from the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     */
    void sendEligiblePackets(struct fcb *fcb);

    /**
     * @brief Check if the packet is the first one of the flow and acts consequently.
     * In particular, it flushes the list of waiting packets and set the sequence number of the
     * next expected packet
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to check
     */
    void checkFirstPacket(struct fcb *fcb, Packet* packet);

    /**
     * @brief Flush the list of waiting packets
     * @param fcb A pointer to the FCB of the flow
     */
    void flushList(struct fcb *fcb);

    /**
     * @brief Flush the list of waiting packets after a given packet
     * @param fcb A pointer to the FCB of the flow
     * @param toKeep Pointer to the last element that will be kept in the list
     * @param toRemove Pointer to the first element that will be removed
     */
    void flushListFrom(struct fcb *fcb, struct TCPPacketListNode *toKeep,
        struct TCPPacketListNode *toRemove);

    /**
     * @brief Check if a given packet is a retransmission
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to check
     * @return True if the given packet is a retransmission
     */
    bool checkRetransmission(struct fcb *fcb, Packet* packet);

    /**
     * @brief Return the sequence number of the packet that will be received after the given one
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to check
     * @return The sequence number of the packet after the given one
     */
    tcp_seq_t getNextSequenceNumber(Packet* packet);

    /**
     * @brief Sort the list of waiting packets using merge sort
     * @param list A pointer to the head of the list of waiting packets
     * @return A pointer to the new head of the list of waiting packets
     */
    TCPPacketListNode* sortList(TCPPacketListNode *list);

    unsigned int flowDirection;
    bool mergeSort;

    per_thread<MemoryPool<struct TCPPacketListNode>> pool;
};

CLICK_ENDDECLS

#endif
