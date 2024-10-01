#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include "tcpreorder.hh"

CLICK_DECLS

TCPReorder::TCPReorder() : _mergeSort(false),_notimeout(false),_verbose(false)
{
}

TCPReorder::~TCPReorder()
{
}

int
TCPReorder::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if(Args(conf, this, errh)
    .read_p("MERGESORT", _mergeSort)
    .read_p("NOTIMEOUT",_notimeout)
    .read_p("VERBOSE",_verbose)
    .complete() < 0)
        return -1;

    VirtualFlowManager::fcb_builded_init_future()->post(
        [this](ErrorHandler* errh){
            return reorder_initialize(errh);
        },this
    );

    return 0;
}


int
TCPReorder::reorder_initialize(ErrorHandler *errh) {
    ElementCastTracker track(router(), "TCPIn");
    router()->visit_downstream(this,0,&track);
    /*if (track.size() == 0) {
        errh->warning("Found no downward TCPIn. This element will work in standalone mode, having its own recycling. This is usually not desirable.");
    } else if (track.size() == 1) {
        _tcp_context = static_cast<TCPIn*>(track[0]);
    } else {
        errh->warning("Found multiple downward TCPIn. This element will work in standalone mode, having its own recycling. This is usually not desirable.");
    }*/
    if (track.size() > 0) {
        return errh->error("TCPIn now includes support for reordering. Use TCPReorder alone if you only want TCP Reordering");
    }
    return 0;
}

void*
TCPReorder::cast(const char *n) {
   if (strcmp("TCPHelper", n) == 0) {
       return static_cast<TCPHelper*>(this);
   }
   return FlowSpaceElement<fcb_tcpreorder>::cast(n);
}

void TCPReorder::flushListFrom(fcb_tcpreorder *tcpreorder, Packet* toKeep,
    Packet *toRemove)
{
    // toKeep will be the last packet in the list
    if(toKeep != NULL)
        toKeep->set_next(NULL);

    if(toRemove == NULL)
        return;

    // Update the head if the list is going to be empty
    if(tcpreorder->packetList == toRemove)
        tcpreorder->packetList = NULL;

    Packet* next = NULL;

    while(toRemove != NULL)
    {
        next = toRemove->next();

        // Kill packet
        toRemove->kill();
    }
}

void TCPReorder::push_flow(int port, fcb_tcpreorder* tcpreorder, PacketBatch *batch)
{
    //click_chatter("Flow %p, uc %d",tcpreorder,fcb_stack->count());
    // Ensure that the pointer in the FCB is set
    if (!checkFirstPacket(tcpreorder, batch)) {
        //click_chatter("Waited %u, uc %d",tcpreorder->expectedPacketSeq,fcb_stack->count());
        return;
    }

    bool had_awaiting = tcpreorder->packetList != 0;

    //Fast path, if no waiting packets and send everything which is in order
    if (likely(!had_awaiting)) {
        int count = 0;
        Packet* last;
        FOR_EACH_PACKET(batch, packet)
        {
            tcp_seq_t currentSeq = getSequenceNumber(packet);
            // We check if the current packet is the expected one (if not, there is a gap)
            if(currentSeq != tcpreorder->expectedPacketSeq)
            {

                if (count != 0) {
                    PacketBatch* second;
                    batch->cut(last, count, second);
                    output_push_batch(0,batch);
                    batch = second;
                }
                goto unordered;
            }

            // Compute the sequence number of the next packet
            tcpreorder->expectedPacketSeq = getNextSequenceNumber(packet);
            tcpreorder->lastSent = currentSeq;
            ++count;
            last = packet;
        }

        //If we are here, everything is in order
        output_push_batch(0,batch);
        //click_chatter("Pushed ordered batch flow %p uc %d",tcpreorder, fcb_stack->count());
        return;

    }
    unordered:
    if (unlikely(_verbose))
        click_chatter("Flow is now unordered... Awaiting %lu, have %lu", tcpreorder->expectedPacketSeq, getSequenceNumber(batch->first()));

    int num = batch->count();
    // Complexity: O(k) (k elements in the batch)
    FOR_EACH_PACKET_SAFE(batch, packet)
    {
        if (unlikely(isRst(packet))) {
            if (_verbose)
                click_chatter("Resetting the flow (have %d packets)!");
            killList(tcpreorder);
            tcpreorder->expectedPacketSeq = 0;
            Packet* next = packet->next();
            while(next) {
                Packet* tmp = next;
                next = next->next();
                tmp->kill();
            }
            checked_output_push_batch(0,PacketBatch::make_from_packet(packet));
            return;
        }
        //click_chatter("TCPReorder : processing Packet %p seq %d flow %p",packet,getSequenceNumber(packet),tcpreorder);

        if(!checkRetransmission(tcpreorder, packet, false)) {
            //click_chatter("Packet is retransmit!");
            num --;
            continue;
        }

        if(_mergeSort)
        {
            // Add the packet at the beginning of the list (unsorted) (O(1))
            packet->set_next(tcpreorder->packetList);
            tcpreorder->packetList = packet;
        }
        else
        {
            if (!putPacketInList(tcpreorder, packet)) {
                num--;
                continue;
            }
        }
    }


    tcpreorder->packetListLength += num;
    int before = tcpreorder->packetListLength;
    if(_mergeSort)
    {
        // Sort the list of waiting packets (O((n + k) * log(n + k)))
        tcpreorder->packetList = sortList(tcpreorder->packetList);
    }
    //click_chatter("flow %p uc %d, num %d",tcpreorder, fcb_stack->count(),num);

    PacketBatch* inorderBatch = sendEligiblePackets(tcpreorder,had_awaiting);


    if (tcpreorder->packetList) {
        assert(tcpreorder->packetListLength);
    }
    /*
    Packet* p = tcpreorder->packetList;
    int c = 0;
    while (p != 0) {
        c++;
        p = p->next();
    }
    assert(tcpreorder->packetListLength == c);
    click_chatter("reorder acquire, %d in list, awaiting %lu",tcpreorder->packetListLength,tcpreorder->expectedPacketSeq);
    */
    fcb_update((before - tcpreorder->packetListLength) - num);
    if (inorderBatch) {
        //click_chatter("Hole is filled, flushing %d", inorderBatch->count() );
        output_push_batch(0,inorderBatch);
    }
}



bool TCPReorder::checkRetransmission(struct fcb_tcpreorder *tcpreorder, Packet* packet, bool always_retransmit)
{
    // If we receive a packet with a sequence number lower than the expected one
    // (taking into account the wrapping sequence numbers), we consider to have a
    // retransmission
    if(SEQ_LT(getSequenceNumber(packet), tcpreorder->expectedPacketSeq))
    {
        if (_verbose) {
            click_chatter("Retransmission ! Sequence is %lu, expected %lu", getSequenceNumber(packet), tcpreorder->expectedPacketSeq);
        }
        if (SEQ_GT(getNextSequenceNumber(packet), tcpreorder->expectedPacketSeq)) {
            //If the packets overlap expectedPacketSeq, this is a split retransmission and we need to keep the good part of the packet
            if (_verbose) {
                click_chatter("Split retransmission !");
                return true;
            }
        }
        // If always_retransmit is not set:
        // We do not send the packet to the second output if the retransmission is a packet
        // that has not already been sent to the next element. In this case, this is a
        // retransmission for a packet we already have in the waiting list so we can discard
        // the retransmission
        // Always retransmit will be set for SYN, as the whole list will be flushed
        if(noutputs() == 2 && (always_retransmit || SEQ_GEQ(getSequenceNumber(packet), tcpreorder->lastSent)))
        {
            PacketBatch *batch = PacketBatch::make_from_packet(packet);
            output_push_batch(1, batch);
        }
        else
            packet->kill();
        return false;
    }

    return true;
}


/**
 * @pre packetListLength is correct
 * @post it is still correct
 */
PacketBatch* TCPReorder::sendEligiblePackets(struct fcb_tcpreorder *tcpreorder, bool had_awaiting)
{
    Packet* packet = tcpreorder->packetList;
    Packet* last = 0;
    PacketBatch* batch = NULL;
    int count = 0;
    while(packet != NULL)
    {
        tcp_seq_t currentSeq = getSequenceNumber(packet);

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
            click_chatter("Warning: received a retransmission with a different split (current %lu, expected %lu, last %lu)",currentSeq, tcpreorder->expectedPacketSeq, (last?getSequenceNumber(last):0));
            Packet* to_delete = packet;
            int num = 0;
            SFCB_STACK( //Do not drop reference as they are from the waiting list packets that are unreferenced
            while(to_delete) {
                packet = to_delete->next();
                to_delete->kill();
                to_delete = packet;
                num++;
            });
            packet = 0;
            fcb_acquire(num); //Compensate for the future update
            tcpreorder->packetListLength -= num;

            // Check before exiting that we did not have a batch to send
            goto send_batch;
        }

        // We check if the current packet is the expected one (if not, there is a gap)
        if(currentSeq != tcpreorder->expectedPacketSeq)
        {
            if (_verbose)
                click_chatter("Not the expected packet, have %d expected %d, last sent is %d. Count is %d. Uc %d",currentSeq,tcpreorder->expectedPacketSeq,tcpreorder->lastSent, count,fcb_stack->count());
            //The packet 
//            if (getNextSequenceNumber(
            tcpreorder->packetList = packet;
            // Check before exiting that we did not have a batch to send
            // TODO : Send pro-active retransmit if option
            goto send_batch;
        }

        // Compute the sequence number of the next packet
        tcpreorder->expectedPacketSeq = getNextSequenceNumber(packet);

        // Store the sequence number of the last packet sent
        tcpreorder->lastSent = currentSeq;

        // Send packet
        if(batch == NULL)
            batch = PacketBatch::start_head(packet);
        count++;

        // Free memory and remove node from the list
        last = packet;
        packet = packet->next();
    }
    tcpreorder->packetList = 0;

    if (unlikely(last && !_notimeout)) { //End of flow, everything will be sent and we manage the flow ourself, remove timeout
        click_ip *ip = (click_ip *) last->data();
        unsigned hlen = ip->ip_hl << 2;
        click_tcp *th = (click_tcp *) (((char *)ip) + hlen);
        //click_chatter("Last packet, releasing timeout");
        if (th->th_flags & (TH_FIN | TH_RST))
                ctx_release_timeout();
    }

  send_batch:
  if (!tcpreorder->packetList && had_awaiting) {
      //We don't have awaiting packets anymore, remove the fct
      //click_chatter("We are now in order, removing release fct");
#if HAVE_FLOW_DYNAMIC
      fcb_remove_release_fnt(tcpreorder,&fcb_release_fnt);
#endif
  } else if (tcpreorder->packetList && !had_awaiting) {
      //Set release fnt
      if (_verbose)
          click_chatter("Out of order, setting release fct");
#if HAVE_FLOW_DYNAMIC
      fcb_set_release_fnt(static_cast<FlowReleaseChain*>(tcpreorder), &fcb_release_fnt);
#endif
  }
    assert(tcpreorder->expectedPacketSeq);
    tcpreorder->packetList = packet;
    tcpreorder->packetListLength -= count;
    // We now send the batch we just built
    if(batch != NULL) {
        assert(count > 0);
        batch->set_count(count);
        batch->set_tail(last);
        last->set_next(0);
        flow_assert(batch->first()->find_count() == count);

        return batch;
    }
    return 0;
}

bool TCPReorder::putPacketInList(struct fcb_tcpreorder* tcpreorder, Packet* packetToAdd)
{
    Packet* last = NULL;
    Packet* packetNode = tcpreorder->packetList;
    auto pSeq = getSequenceNumber(packetToAdd);
    // Browse the list until we find a packet with a greater sequence number than the
    // packet to add in the list
    while(packetNode != NULL
        && (SEQ_LT(getSequenceNumber(packetNode), pSeq)))
    {
        last = packetNode;
        packetNode = packetNode->next();
    }

    if (packetNode && (getSequenceNumber(packetNode) == pSeq)) {
        packetToAdd -> kill();
        return false;
    }

    //SEQ_LT(getAckNumber(packetNode), getAckNumber(packetToAdd)))

    // Check if we need to add the node as the head of the list
    if(last == NULL)
        tcpreorder->packetList = packetToAdd; // If so, the list points to the node to add
    else
        last->set_next(packetToAdd); // If not, the previous node in the list now points to the node to add

     // The node to add points to the first node with a greater sequence number
    packetToAdd->set_next(packetNode);
    return true;
}

void TCPReorder::killList(struct fcb_tcpreorder* tcpreorder) {
        SFCB_STACK( //Packet in the list have no reference
            FOR_EACH_PACKET_SAFE(((PacketBatch*)(tcpreorder->packetList)),p) {
                click_chatter("WARNING : Non-free TCPReorder flow bucket");
                p->kill();
            }
        );
        tcpreorder->packetList = 0;
        tcpreorder->packetListLength = 0;
}

bool TCPReorder::checkFirstPacket(struct fcb_tcpreorder* tcpreorder, PacketBatch* batch)
{
    Packet* packet = batch->first();
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet is a SYN packet
    if(flags & TH_SYN)
    {
        if (tcpreorder->expectedPacketSeq) {
            Packet* next = packet->next();

            //click_chatter("Potential duplicate syn received (%u), fcb uc %d", tcpreorder->expectedPacketSeq,fcb_stack->count());
            if (!checkRetransmission(tcpreorder, packet, true)) { //If SYN retransmission, the current packet will be killed.
                //click_chatter("Syn retransmission ! Deleting the rest of the batch!");
                while(next) {
                    packet = next;
                    next = packet->next();
                    packet->kill();
                }
                return false;
            } else {
                //click_chatter("But seems good...");
            }
        }

        // Update the expected sequence number
        tcpreorder->expectedPacketSeq = getSequenceNumber(packet);
        //click_chatter("First packet received (%u) for flow %p", tcpreorder->expectedPacketSeq,fcb_stack);

        //If there is no better TCP manager, we must ensure the flow stays alive
        if (!_notimeout) {
            ctx_acquire_timeout(2000);
        }

        // Ensure that the list of waiting packets is free
        // (SYN should always be the first packet)
        killList(tcpreorder);
    } else {
        if (!tcpreorder->expectedPacketSeq) {
            //click_chatter("The flow does not start with a syn! We should send a RST sometime !"); //No, this is the role of the tcp ctx
            if (isRst(batch->first())) { //Let the RST pass for a bad SYN
                if (_verbose)
                    click_chatter("Resetting the flow !");
                killList(tcpreorder);
                tcpreorder->expectedPacketSeq = 0;

                if (batch->count() == 1) {
                    checked_output_push_batch(0,batch);
                    return false;
                } else { //A RST and something else smells the attack... Discard
                    batch->fast_kill();
                    return false;
                }
            } else {
                batch->fast_kill();
                return false;
            }
        }
    }
    return true;
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
Packet* TCPReorder::sortList(Packet *list)
{
    Packet *p, *q, *e, *tail;
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
                q = q->next();
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
                    q = q->next();
                    qsize--;
                }
                else if(qsize == 0 || !q)
                {
                    /* q is empty; e must come from p. */
                    e = p;
                    p = p->next();
                    psize--;
                }
                else if(SEQ_LT(getSequenceNumber(p), getSequenceNumber(q)))
                {
                    /* First element of p is lower (or same);
                     * e must come from p. */
                    e = p;
                    p = p->next();
                    psize--;
                } /* else if (getSequenceNumber(p) == getSequenceNumber(q)
                        && SEQ_LT(getAckNumber(p), getAckNumber(q))) {
                    //TODO
                    assert(false);
                    p->kill();
                }*/
                else
                {
                    /* First element of q is lower; e must come from q. */
                    e = q;
                    q = q->next();
                    qsize--;
                }

                /* add the next element to the merged list */
                if(tail)
                    tail->set_next(e);
                else
                    list = e;

                tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }

        tail->set_next(NULL);

        /* If we have done only one merge, we're finished. */
        if(nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
}

void TCPReorder::fcb_release_fnt(FlowControlBlock* fcb, void* thunk) {
    TCPReorder* tr = static_cast<TCPReorder*>(thunk);
    if (tr->_verbose)
        click_chatter("Flushing %p{element}, data off %d",tr,tr->_flow_data_offset);
    fcb_tcpreorder* tcpreorder = reinterpret_cast<fcb_tcpreorder*>(&fcb->data[tr->_flow_data_offset]);

    int i = 0;
    if (tcpreorder->packetList) {
        FOR_EACH_PACKET_LL_SAFE(tcpreorder->packetList,p) {
            p->kill();
            i++;
        }
    }

    //click_chatter("Released %d",i);
    tcpreorder->packetList = 0;
#if HAVE_FLOW_DYNAMIC
    if (tcpreorder->previous_fnt)
        tcpreorder->previous_fnt(fcb, tcpreorder->previous_thunk);
#endif
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPReorder)
ELEMENT_MT_SAFE(TCPReorder)
