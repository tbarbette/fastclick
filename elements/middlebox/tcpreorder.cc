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
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif

    for(unsigned int i = 0; i < pool.weight(); ++i)
        pool.get_value(i).initialize(TCPREORDER_POOL_SIZE);

    mergeSort = true;
}

TCPReorder::~TCPReorder()
{
}

int TCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
    .read_mp("FLOWDIRECTION", flowDirectionParam)
    .read_p("MERGESORT", mergeSort)
    .complete() < 0)
        return -1;

    flowDirection = (unsigned int)flowDirectionParam;

    return 0;
}

void TCPReorder::flushList(fcb_tcpreorder *tcpreorder)
{
    struct TCPPacketListNode* head = tcpreorder->packetList;

    // Flush the entire list
    flushListFrom(tcpreorder, NULL, head);
}

void TCPReorder::flushListFrom(fcb_tcpreorder *tcpreorder, struct TCPPacketListNode *toKeep,
    struct TCPPacketListNode *toRemove)
{
    // toKeep will be the last packet in the list
    if(toKeep != NULL)
        toKeep->next = NULL;

    if(toRemove == NULL)
        return;

    // Update the head if the list is going to be empty
    if(tcpreorder->packetList == toRemove)
        tcpreorder->packetList = NULL;

    struct TCPPacketListNode* toFree = NULL;

    while(toRemove != NULL)
    {
        toFree = toRemove;
        toRemove = toRemove->next;

        // Kill packet
        if(toFree->packet != NULL)
            toFree->packet->kill();

        tcpreorder->pool->releaseMemory(toFree);
    }
}

void TCPReorder::push_batch(int port, fcb_tcpreorder* tcpreorder, PacketBatch *batch)
{
    // Ensure that the pointer in the FCB is set
    if(tcpreorder->pool == NULL)
        tcpreorder->pool = &(*pool);

    // Complexity: O(k) (k elements in the batch)
    FOR_EACH_PACKET_SAFE(batch, packet)
    {
        checkFirstPacket(tcpreorder, packet);

        if(!checkRetransmission(tcpreorder, packet))
            continue;

        if(mergeSort)
        {
            // Add the packet at the beginning of the list (unsorted) (O(1))
            struct TCPPacketListNode* toAdd = tcpreorder->pool->getMemory();

            toAdd->packet = packet;
            toAdd->next = tcpreorder->packetList;
            tcpreorder->packetList = toAdd;
        }
        else
            putPacketInList(tcpreorder, packet); // Put the packet directly at the right position O(n + k)
    }

    if(mergeSort)
    {
        // Sort the list of waiting packets (O((n + k) * log(n + k)))
        tcpreorder->packetList = sortList(tcpreorder->packetList);
    }

    sendEligiblePackets(tcpreorder);
}



bool TCPReorder::checkRetransmission(struct fcb_tcpreorder *tcpreorder, Packet* packet)
{
    // If we receive a packet with a sequence number lower than the expected one
    // (taking into account the wrapping sequence numbers), we consider to have a
    // retransmission
    if(SEQ_LT(getSequenceNumber(packet), tcpreorder->expectedPacketSeq))
    {
        // We do not send the packet to the second output if the retransmission is a packet
        // that has not already been sent to the next element. In this case, this is a
        // retransmission for a packet we already have in the waiting list so we can discard
        // the retransmission
        if(noutputs() == 2 && SEQ_GEQ(getSequenceNumber(packet), tcpreorder->lastSent))
        {
            #if HAVE_BATCH
                PacketBatch *batch = PacketBatch::make_from_packet(packet);
                if(batch != NULL)
                    output_push_batch(1, batch);
            #else
                output(1).push(packet);
            #endif
        }
        else
            packet->kill();
        return false;
    }

    return true;
}

void TCPReorder::sendEligiblePackets(struct fcb_tcpreorder *tcpreorder)
{
    struct TCPPacketListNode* packetNode = tcpreorder->packetList;

    #if HAVE_BATCH
        PacketBatch* batch = NULL;
    #endif

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
        // correctly aligned and must be dropped as the source will retransmit
        // them with the new alignment.
        if(SEQ_LT(currentSeq, tcpreorder->expectedPacketSeq))
        {
            click_chatter("Warning: received a retransmission with a different split");
            #ifndef HAVE_FLOW
            // Warning if the system is used outside middleclick
            click_chatter("This may be the sign that a second flow is interfering, this can cause bugs.");
            #endif
            flushListFrom(tcpreorder, NULL, packetNode);
            #if HAVE_BATCH
                // Check before exiting that we did not have a batch to send
                if(batch != NULL)
                    output_push_batch(0, batch);
            #endif
            return;
        }

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != tcpreorder->expectedPacketSeq)
        {
            #if HAVE_BATCH
                // Check before exiting that we did not have a batch to send
                if(batch != NULL)
                    output_push_batch(0, batch);
            #endif
            return;
        }

        // Compute the sequence number of the next packet
        tcpreorder->expectedPacketSeq = getNextSequenceNumber(packetNode->packet);

        // Store the sequence number of the last packet sent
        tcpreorder->lastSent = currentSeq;

        // Send packet
        #if HAVE_BATCH
            // If we use batching, we build the batch while browsing the list
            if(batch == NULL)
                batch = PacketBatch::make_from_packet(packetNode->packet);
            else
                batch->append_packet(packetNode->packet);
        #else
            // If we don't use batching, push the packet immediately
            output(0).push(packetNode->packet);
        #endif

        // Free memory and remove node from the list
        struct TCPPacketListNode* toFree = packetNode;
        packetNode = packetNode->next;
        tcpreorder->packetList = packetNode;
        tcpreorder->pool->releaseMemory(toFree);
    }

    #if HAVE_BATCH
        // We now send the batch we just built
        if(batch != NULL)
            output_push_batch(0, batch);
    #endif
}

tcp_seq_t TCPReorder::getNextSequenceNumber(Packet* packet)
{
    tcp_seq_t currentSeq = getSequenceNumber(packet);

    // Compute the size of the current payload
    tcp_seq_t nextSeq = currentSeq + getPayloadLength(packet);

    // FIN and SYN packets count for one in the sequence number
    if(isFin(packet) || isSyn(packet))
        nextSeq++;

    return nextSeq;
}

void TCPReorder::putPacketInList(struct fcb_tcpreorder* tcpreorder, Packet* packet)
{
    struct TCPPacketListNode* prevNode = NULL;
    struct TCPPacketListNode* packetNode = tcpreorder->packetList;
    struct TCPPacketListNode* toAdd = tcpreorder->pool->getMemory();
    toAdd->packet = packet;
    toAdd->next = NULL;

    // Browse the list until we find a packet with a greater sequence number than the
    // packet to add in the list
    while(packetNode != NULL
        && (SEQ_LT(getSequenceNumber(packetNode->packet), getSequenceNumber(packet))
            || (getSequenceNumber(packetNode->packet) == getSequenceNumber(packet)
                && SEQ_LT(getAckNumber(packetNode->packet), getAckNumber(packet)))))
    {
        prevNode = packetNode;
        packetNode = packetNode->next;
    }

    // Check if we need to add the node as the head of the list
    if(prevNode == NULL)
        tcpreorder->packetList = toAdd; // If so, the list points to the node to add
    else
        prevNode->next = toAdd; // If not, the previous node in the list now points to the node to add

     // The node to add points to the first node with a greater sequence number
    toAdd->next = packetNode;
}

void TCPReorder::checkFirstPacket(struct fcb_tcpreorder* tcpreorder, Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if(flags & TH_SYN)
    {
        // Update the expected sequence number
        tcpreorder->expectedPacketSeq = getSequenceNumber(packet);
        click_chatter("First packet received (%u) for flow %u", tcpreorder->expectedPacketSeq,
            flowDirection);

        // Ensure that the list of waiting packets is free
        // (SYN should always be the first packet)
        flushList(tcpreorder);
    }
}

/*
 * This method is based on the work of Simon Tatham
 * which is copyright 2001 Simon Tatham, under MIT license.
 * http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
TCPPacketListNode* TCPReorder::sortList(TCPPacketListNode *list)
{
    TCPPacketListNode *p, *q, *e, *tail;
    int insize, nmerges, psize, qsize, i;

    /*
    * Silly special case: if `list' was passed in as NULL, return
    * NULL immediately.
    */
   if(!list)
    return NULL;

    insize = 1;

    while(true)
    {
        p = list;
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while(p)
        {
            nmerges++;  /* there exists a merge to be done */
            /* step `insize' places along from p */
            q = p;
            psize = 0;
            for(i = 0; i < insize; i++)
            {
                psize++;
                q = q->next;
                if(!q)
                    break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while(psize > 0 || (qsize > 0 && q))
            {
                /* decide whether next element of merge comes from p or q */
                if(psize == 0)
                {
                    /* p is empty; e must come from q. */
                    e = q;
                    q = q->next;
                    qsize--;
                }
                else if(qsize == 0 || !q)
                {
                    /* q is empty; e must come from p. */
                    e = p;
                    p = p->next;
                    psize--;
                }
                else if(SEQ_LT(getSequenceNumber(p->packet), getSequenceNumber(q->packet))
                    || ((getSequenceNumber(p->packet) == getSequenceNumber(q->packet))
                        && SEQ_LT(getAckNumber(p->packet), getAckNumber(q->packet))))
                {
                    /* First element of p is lower (or same);
                     * e must come from p. */
                    e = p;
                    p = p->next;
                    psize--;
                }
                else
                {
                    /* First element of q is lower; e must come from q. */
                    e = q;
                    q = q->next;
                    qsize--;
                }

                /* add the next element to the merged list */
                if(tail)
                    tail->next = e;
                else
                    list = e;

                tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }

        tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if(nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPReorder)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_MT_SAFE(TCPReorder)
