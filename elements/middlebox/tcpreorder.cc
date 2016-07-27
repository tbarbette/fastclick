/*
 * tcpreorder.{cc,hh} -- reorder TCP packets
 * Romain Gaillard
 *
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreorder.hh"
#include "tcpelement.hh"

CLICK_DECLS

TCPReorder::TCPReorder()
{
    #if HAVE_BATCH
        in_batch_mode = BATCH_MODE_YES;
    #endif

    for(unsigned int i = 0; i < pool.size(); ++i)
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

void TCPReorder::flushList(struct fcb *fcb)
{
    struct TCPPacketListNode* head = fcb->tcpreorder.packetList;

    // Flush the entire list
    flushListFrom(fcb, NULL, head);
}

void TCPReorder::flushListFrom(struct fcb *fcb, struct TCPPacketListNode *toKeep,
    struct TCPPacketListNode *toRemove)
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

void TCPReorder::push_packet(int, Packet *packet)
{
    processPacket(&fcbArray[flowDirection], packet);
}

#if HAVE_BATCH
void TCPReorder::push_batch(int port, PacketBatch *batch)
{
    if(mergeSort)
        processPacketBatch(&fcbArray[flowDirection], batch);
    else
    {
        FOR_EACH_PACKET_SAFE(batch, packet)
            processPacket(&fcbArray[flowDirection], packet);
    }
}
#endif

void TCPReorder::processPacket(struct fcb *fcb, Packet* packet)
{
    // Ensure that the pointer in the FCB is set
    if(fcb->tcpreorder.pool == NULL)
        fcb->tcpreorder.pool = &(*pool);

    checkFirstPacket(fcb, packet);
    if(!checkRetransmission(fcb, packet))
        return;
    putPacketInList(fcb, packet);
    sendEligiblePackets(fcb);
}

void TCPReorder::processPacketBatch(struct fcb *fcb, PacketBatch* batch)
{
    // Ensure that the pointer in the FCB is set
    if(fcb->tcpreorder.pool == NULL)
        fcb->tcpreorder.pool = &(*pool);

    FOR_EACH_PACKET_SAFE(batch, packet)
    {
        checkFirstPacket(fcb, packet);

        if(!checkRetransmission(fcb, packet))
            continue;

        // Add the packet at the beginning of the list unsorted (O(1))
        struct TCPPacketListNode* toAdd = fcb->tcpreorder.pool->getMemory();

        toAdd->packet = packet;
        toAdd->next = fcb->tcpreorder.packetList;
        fcb->tcpreorder.packetList = toAdd;
    }

    // Sort the list of waiting packets (O(n*log(n)))
    fcb->tcpreorder.packetList = sortList(fcb->tcpreorder.packetList);

    sendEligiblePackets(fcb);
}

bool TCPReorder::checkRetransmission(struct fcb *fcb, Packet* packet)
{
    // If we receive a packet with a sequence number lower than the expected one
    // (taking into account the wrapping sequence numbers), we consider to have a
    // retransmission
    if(SEQ_LT(getSequenceNumber(packet), fcb->tcpreorder.expectedPacketSeq))
    {
        if(noutputs() == 2)
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

void TCPReorder::sendEligiblePackets(struct fcb *fcb)
{
    struct TCPPacketListNode* packetNode = fcb->tcpreorder.packetList;

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
        // correctly align and must be dropped as the source will retransmit
        // them with the new alignment.
        if(SEQ_LT(currentSeq, fcb->tcpreorder.expectedPacketSeq))
        {
            click_chatter("Warning: received a retransmission with a different split");
            flushListFrom(fcb, NULL, packetNode);
            #if HAVE_BATCH
                // Check before exiting that we did not have a batch to send
                if(batch != NULL)
                    output_push_batch(0, batch);
            #endif
            return;
        }

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != fcb->tcpreorder.expectedPacketSeq)
        {
            #if HAVE_BATCH
                // Check before exiting that we did not have a batch to send
                if(batch != NULL)
                    output_push_batch(0, batch);
            #endif
            return;
        }

        // Compute sequence number of the next packet
        fcb->tcpreorder.expectedPacketSeq = getNextSequenceNumber(packetNode->packet);

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
        fcb->tcpreorder.packetList = packetNode;
        fcb->tcpreorder.pool->releaseMemory(toFree);
    }

    #if HAVE_BATCH
        // We now send the batch we built
        if(batch != NULL)
            output_push_batch(0, batch);
    #endif
}

tcp_seq_t TCPReorder::getSequenceNumber(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
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

void TCPReorder::putPacketInList(struct fcb* fcb, Packet* packet)
{
    struct TCPPacketListNode* prevNode = NULL;
    struct TCPPacketListNode* packetNode = fcb->tcpreorder.packetList;
    struct TCPPacketListNode* toAdd = fcb->tcpreorder.pool->getMemory();
    toAdd->packet = packet;
    toAdd->next = NULL;

    // Browse the list until we find a packet with a greater sequence number than the
    // packet to add in the list
    while(packetNode != NULL
        && (SEQ_LT(getSequenceNumber(packetNode->packet), getSequenceNumber(packet))))
    {
        prevNode = packetNode;
        packetNode = packetNode->next;
    }

    // Check if we need to add the node at the head of the list
    if(prevNode == NULL)
        fcb->tcpreorder.packetList = toAdd; // If so, the list points to the node to add
    else
        prevNode->next = toAdd; // If not, the previous node in the list now points to the node to add

     // The node to add points to the first node with a greater sequence number
    toAdd->next = packetNode;
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
        click_chatter("First packet received (%u) for flow %u", fcb->tcpreorder.expectedPacketSeq,
            flowDirection);

        // Ensure that the list of waiting packets is free
        // (SYN should always be the first packet)
        flushList(fcb);
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
                else if(SEQ_LT(getSequenceNumber(p->packet), getSequenceNumber(q->packet)))
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
