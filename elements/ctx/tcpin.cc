#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <click/timestamp.hh>
#include "tcpin.hh"
#include <click/ipelement.hh>

CLICK_DECLS

#define TCP_TIMEOUT 30000

TCPIn::TCPIn() : outElement(NULL), returnElement(NULL),_retransmit(0),
tableFcbTcpCommon(), _verbose(false), _reorder(true), _proactive_dup(false),_retransmit_pt(false)
{
    poolModificationTracker.static_initialize();

    poolFcbTcpCommon.static_initialize();
    // Initialize the memory pools of each thread
    for(unsigned int i = 0; i < poolModificationNodes.weight(); ++i)
        poolModificationNodes.get_value(i).initialize(MODIFICATIONNODES_POOL_SIZE);

    for(unsigned int i = 0; i < poolModificationLists.weight(); ++i)
        poolModificationLists.get_value(i).initialize(MODIFICATIONLISTS_POOL_SIZE);

    // Warning about the fact that the system must be integrated to Middleclick in order to
    // work properly.
#ifndef HAVE_FLOW
    click_chatter("WARNING: You are using a version of this program that is not compatible"
            " with the flow management system provided by Middleclick. Therefore, you will"
            " only be able to use one flow.");
#endif
}

TCPIn::~TCPIn() {

}

int TCPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String returnName = "";
    String outName = "";
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
            .read_mp("RETURNNAME", returnName)
            .read("FLOWDIRECTION", flowDirectionParam)
            .read("REORDER", _reorder)
            .read("OUTNAME", outName)
            .read("VERBOSE", _verbose)
            .read("PROACK", _proactive_dup)
            .read("RETRANSMIT_PT",_retransmit_pt)
            .complete() < 0)
        return -1;

    Element* returnElement = this->router()->find(returnName, errh);
    Element* outElement;
    if (!outName) {
        ElementCastTracker visitor(router(),"TCPOut");
        router()->visit_downstream(this, -1, &visitor);
        if (visitor.size() != 1) {
            return errh->error("Found no or more than 1 TCPOut element. Specify which one to use with OUTNAME");
        }
        outElement = visitor[0];
    } else {
        outElement = this->router()->find(outName, errh);
    }

    if (noutputs() == 1 && !_retransmit_pt) {
        ElementCastTracker visitor(router(),"TCPRetransmitter");
        router()->visit_downstream(this, -1, &visitor);
        if (visitor.size() != 1) {
            errh->warning("TCPIn has only one output and I could not find a TCPRetransmitter. TCP retransmissions will be lost and not forwarded");
        } else {
            _retransmit[0].assign_peer(true, visitor[0], 1, false);
        }
    }

    if(returnElement == NULL)
    {
        click_chatter("Error: Could not find TCPIn element "
                "called \"%s\".", returnName.c_str());
        return -1;
    }
    else if(outElement == NULL)
    {
        click_chatter("Error: Could not find TCPOut element "
                "called \"%s\".", outName.c_str());
        return -1;
    }
    else if(strcmp("TCPIn", returnElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPIn element "
                "but a %s element.", returnName.c_str(), returnElement->class_name());
        return -1;
    }
    else if(strcmp("TCPOut", outElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPOut element "
                "but a %s element.", outName.c_str(), outElement->class_name());
        return -1;
    }
    else if(flowDirectionParam != 0 && flowDirectionParam != 1 && flowDirectionParam != -1)
    {
        click_chatter("Error: FLOWDIRECTION %u is not valid.", flowDirectionParam);
        return -1;
    }
    else
    {
        this->returnElement = (TCPIn*)returnElement;
        this->outElement = (TCPOut*)outElement;
        this->returnElement->add_remote_element(this);
        this->outElement->add_remote_element(this);
        if (this->outElement->setInElement(this, errh) != 0)
            return -1;
        if (flowDirectionParam == -1) {
            if (this->returnElement->getFlowDirection() == 0 || this->returnElement->getFlowDirection() == -1) {
                flowDirectionParam = this->returnElement->getOppositeFlowDirection();
            } else {
                flowDirectionParam = 0;
            }
        }
        setFlowDirection((unsigned int)flowDirectionParam);
        this->outElement->setFlowDirection(getFlowDirection());
    }

    //Set the initialization to run after all stack objects passed
    allStackInitialized.post(new Router::FctFuture([this](ErrorHandler* errh) {
        return tcp_initialize(errh);
    }, this));

    return 0;
}

/**
 * Check if a packet is a retransmission
 * @return False if packet is a retransmission. In which case it has been consumed
 */
bool TCPIn::checkRetransmission(struct fcb_tcpin *tcpreorder, Packet* packet, bool always_retransmit)
{
    if (_retransmit_pt)
        return true;

    // If we receive a packet with a sequence number lower than the expected one
    // (taking into account the wrapping sequence numbers), we consider to have a
    // retransmission
    if(SEQ_LT(getSequenceNumber(packet), tcpreorder->expectedPacketSeq))
    {
        if (unlikely(_verbose > 2)) {
            click_chatter("Retransmission ! Sequence is %lu, expected %lu, last sent %lu. Syn %d. Ack %d.", getSequenceNumber(packet), tcpreorder->expectedPacketSeq, tcpreorder->lastSent,isSyn(packet),isAck(packet));
        }
        if (SEQ_GT(getNextSequenceNumber(packet), tcpreorder->expectedPacketSeq)) {
            //If the packets overlap expectedPacketSeq, this is a split retransmission and we need to keep the good part of the packet
            if (unlikely(_verbose)) {
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
        if(noutputs() == 2 && (always_retransmit || SEQ_LEQ(getSequenceNumber(packet), tcpreorder->lastSent)))
        {
            PacketBatch *batch = PacketBatch::make_from_packet(packet);
            output_push_batch(1, batch);
        }
        else
        {
            if (_retransmit) {
                if (unlikely(_verbose))
                    click_chatter("Retransmit passed to retransmitter");
                _retransmit->push_batch(PacketBatch::make_from_packet(packet));
            } else {
                if (unlikely(_verbose))
                    click_chatter("Killed retransmission");
                packet->kill();
            }
        }
        return false;
    }

    return true;
}

void TCPIn::print_packet(const char* text, struct fcb_tcpin* fcb_in, Packet* p) {
                    click_chatter("%s (state %d, is_ack : %d, src %s:%d dst %s:%dt", text, fcb_in->common->state, isAck(p),
                                IPAddress(p->ip_header()->ip_src).unparse().c_str(), ntohs(p->tcp_header()->th_sport),
                                IPAddress(p->ip_header()->ip_dst).unparse().c_str(), ntohs(p->tcp_header()->th_dport));


}

/**
 * Put packet in unordered list, directly at the right place
 */
bool TCPIn::putPacketInList(struct fcb_tcpin* tcpreorder, Packet* packetToAdd)
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
        if (unlikely(_verbose))
            click_chatter("BAD ERROR : A retransmit passed through");
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

    //Packet in list have no FCB reference
    fcb_release(1);
    return true;
}

/**
 * Update state and stuffs for a packet in order
 * @return The same or a different (because of uniqueify) packet
 *         Null if the connection was closed
 */
Packet*
TCPIn::processOrderedTCP(fcb_tcpin* fcb_in, Packet* p) {
    if(checkConnectionClosed(p))
    {
        if (unlikely(_verbose))
            click_chatter("Connection is already closed");
        p->kill();
        return 0;
    }

    tcp_seq_t currentSeq = getSequenceNumber(p);
    fcb_in->lastSent = currentSeq;
    fcb_in->expectedPacketSeq = getNextSequenceNumber(p);

    if (allowResize()) {
        WritablePacket *packet = p->uniqueify();

        // Set the annotation indicating the initial ACK value
        setInitialAck(packet, getAckNumber(packet));

        // Compute the offset of the TCP payload
        uint16_t offset = getPayloadOffset(packet);
        packet->setContentOffset(offset);

        //TODO : fine-grain this lock
        fcb_in->common->lock.acquire();
        ByteStreamMaintainer &maintainer = fcb_in->common->maintainers[getFlowDirection()];
        ByteStreamMaintainer &otherMaintainer = fcb_in->common->maintainers[getOppositeFlowDirection()];

        // Update the window size
        uint16_t prevWindowSize = maintainer.getWindowSize();
        uint16_t newWindowSize = getWindowSize(packet);
        maintainer.setWindowSize(newWindowSize);


        if(otherMaintainer.isLastAckSentSet())
        {
            tcp_seq_t lastAckSentOtherSide = otherMaintainer.getLastAckSent();

            if(!isSyn(packet) && SEQ_LT(currentSeq, lastAckSentOtherSide))
            {
                if (unlikely(_verbose)) {
                    click_chatter("Lost ACK, re-acking. Seq is %lu, other is %lu",currentSeq, lastAckSentOtherSide);
                }
                // We receive content that has already been ACKed.
                // This case occurs when the ACK is lost between the middlebox and
                // the destination.
                // In this case, we re-ACK the content and we discard it
                ackPacket(packet);
                packet->kill();
                fcb_in->common->lock.release();
                return NULL;
            }
        }

        // Take care of the ACK value in the packet
        bool isAnAck = isAck(packet);
        tcp_seq_t ackNumber = 0;
        tcp_seq_t newAckNumber = 0;
        if(isAnAck)
        {
            // Map the ack number according to the ByteStreamMaintainer of the other direction
            ackNumber = getAckNumber(packet);
            newAckNumber = otherMaintainer.mapAck(ackNumber);


            if (unlikely(_verbose))
                click_chatter("Map ACK %lu -> %lu", ackNumber, newAckNumber);

            // Check the value of the previous ack received if it exists
            bool lastAckReceivedSet = fcb_in->common->lastAckReceivedSet();
            uint32_t prevLastAckReceived = 0;
            if(lastAckReceivedSet)
                prevLastAckReceived = fcb_in->common->getLastAckReceived(getFlowDirection());


            // Check if we acknowledged new data
            if(lastAckReceivedSet && SEQ_GT(ackNumber, prevLastAckReceived))
            {
                // Increase congestion window
                uint64_t cwnd = otherMaintainer.getCongestionWindowSize();
                uint64_t ssthresh = otherMaintainer.getSsthresh();
                // Sender segment size
                uint16_t mss = otherMaintainer.getMSS();
                uint64_t increase = 0;

                // Check if we are in slow start mode
                if(cwnd <= ssthresh)
                    increase = mss;
                else
                    increase = mss * mss / cwnd;

                otherMaintainer.setCongestionWindowSize(cwnd + increase);

                maintainer.setDupAcks(0);
            }

            // Update the value of the last ACK received
            fcb_in->common->setLastAckReceived(getFlowDirection(),getAckNumber(p));

            // Prune the ByteStreamMaintainer of the other side
            otherMaintainer.prune(ackNumber);

            // Update the statistics about the RTT
            // And potentially update the retransmission timer
            /*fcb_in->common->lock.release();
            if (has_retrans)
		fcb_in->common->retransmissionTimings[getOppositeFlowDirection()].signalAck(ackNumber);
            fcb_in->common->lock.acquire();*/

            // Check if the current packet is just an ACK without additional information
            if(isJustAnAck(packet) && prevWindowSize == newWindowSize)
            {
                bool isDup = false;
                // Check duplicate acks
                if(prevLastAckReceived == ackNumber)
                {
                    if (unlikely(_verbose)) {
                        click_chatter("DUP Ack !");
                    }
                    isDup = true;
                    uint8_t dupAcks = maintainer.getDupAcks();
                    dupAcks++;
                    maintainer.setDupAcks(dupAcks);

                    // Fast retransmit
                    if(dupAcks >= 3)
                    {
                       /* fcb_in->common->lock.release();
                        if (has_retrans)
				fcb_in->common->retransmissionTimings[getOppositeFlowDirection()].fireNow();
                        fcb_in->common->lock.acquire();*/
                        maintainer.setDupAcks(0);
                    }
                }

                // Check that the ACK value is greater than what we have already
                // sent to the destination.
                // If this is a duplicate ACK, we don't discard it so that the mechanism
                // of fast retransmission is preserved
                if(maintainer.isLastAckSentSet() && SEQ_LEQ(newAckNumber, maintainer.getLastAckSent()) && !isDup)
                {
                    // If this is not the case, the packet does not bring any additional information
                    // We can drop it
                    if (unlikely(_verbose)) {
                        click_chatter("Meaningless ack");
                    }
                    packet->kill();
                    fcb_in->common->lock.release();
                    return NULL;
                }
            }

            // If needed, update the ACK value in the packet with the mapped one
            if(ackNumber != newAckNumber)
                setAckNumber(packet, newAckNumber);
        }
        fcb_in->common->lock.release();

        return packet;
    } else { //Resize not allowed
        // Compute the offset of the TCP payload

        if (isAck(p)) {
            fcb_in->common->setLastAckReceived(getFlowDirection(),getAckNumber(p));
        }
        uint16_t offset = getPayloadOffset(p);
        p->setContentOffset(offset);

        return p;
    }
}

void TCPIn::push_flow(int port, fcb_tcpin* fcb_in, PacketBatch* flow)
{
    // Assign the tcp_common structure if not already done
    //click_chatter("Fcb in : %p, Common : %p, Batch : %p, State %d", fcb_in, fcb_in->common,flow,(fcb_in->common?fcb_in->common->state:-1));
    auto fnt = [this,fcb_in](Packet* p) -> Packet* {
        bool keep_fct = false;
        if(fcb_in->common == NULL)
        {
            eagain:
            if(!assignTCPCommon(p, keep_fct))
            {
                if (isRst(p)) {
                    //click_chatter("RST received");
                    //First packet was a RST
                    outElement->output_push_batch(0, PacketBatch::make_from_packet(p)); //Elements never knew about this flow, we bypass
                    resetReorderer(fcb_in);
                    //TCPReorder will ensure the RST is alone, so we don't need to bother about dropping the rest of the batch, there is none
                } else {
                    // The allocation failed, meaning that the packet is not a SYN
                    // packet. This is not supposed to happen and it means that
                    // the first two packets of the connection are not SYN packets
                    if (_verbose)
                        click_chatter("Warning: Trying to assign a common tcp memory area"
                                " for a non-SYN packet or a non-matching tupple (S: %d, R: %d, A:%d, F:%d, src %s:%d dst %s:%d)",
                                isSyn(p),isRst(p),isAck(p),isFin(p),
                                IPAddress(p->ip_header()->ip_src).unparse().c_str(), ntohs(p->tcp_header()->th_sport),
                                IPAddress(p->ip_header()->ip_dst).unparse().c_str(), ntohs(p->tcp_header()->th_dport));
                    p->kill();
                }

                return NULL;
            }


            //If not syn, drop the flow
            if(!isSyn(p)) { //TODO : move to top block?
                WritablePacket* packet = p->uniqueify();
                closeConnection(packet, false);
                if (unlikely(_verbose))
                    click_chatter("Packet is not SYN, closing connection");
                packet->kill();
                return NULL;
            }
            doopt:

            // Manage the TCP options:
            // - Remove the SACK-permitted option
            // - Detect the window scale
            // - Detect MSS
            WritablePacket* packet = p->uniqueify();
            manageOptions(packet);
            p = packet;

            if (isAck(p) && fcb_in->common->state < TCPState::OPEN) {
                fcb_in->common->lastAckReceived[getFlowDirection()] = getAckNumber(p); //We need to do it now before the GT check for the OPEN state
                //TODO check the side of the ack with ESTABLISHING 1, or an attacker may create ressource sending himself the synack
                fcb_in->common->state = TCPState::OPEN;
            }
        } /* fcb_in->common != NULL */
        else // At least one packet of this side of the flow has been seen, or con has been reset
        {
            // The structure has been assigned so the three-way handshake should be over..

            // .. except if we have retransmission about the handshake, or reusing an old connection
            // Check that the packet is not a SYN packet
            if(isSyn(p))
            {
                if (unlikely(fcb_in->common->state == TCPState::CLOSED)) {
                    if (isAck(p)) {
                        if (unlikely(_verbose))
                            click_chatter("Syn ACK on a CLOSED connection. Send an ACK first...");
                        p->kill();
                        return 0;
                    }

reinitforward:

                    /**
                     * There is a good chance that the other side is still in TIMEWAIT
                     **/
                    fcb_in->common->lock.acquire();
                    auto common = fcb_in->common;

                    if (fcb_in->common->use_count == 2) { //Still valid on the other side, it's in TIMEWAIT
                        if (unlikely(_verbose > 1))
                            print_packet("Reusing socket !", fcb_in, p);

                        SFCB_STACK(
                                releaseFcbSide(fcb_save, fcb_in);
                        );
                        fcb_in->common->reinit();
                        //Todo reuse may be more efficient
                        initializeFcbSide(fcb_in, p, true);

                        if (getFlowDirection() == 1)
                            fcb_in->common->state = TCPState::ESTABLISHING_1;
                        else
                            fcb_in->common->state = TCPState::ESTABLISHING_2;

                        /*
                         *  From here we could have a race condition, if before reusing the state below,
                         * the other side releases itself because of a timeout from flow that cannot
                         * be stopped.
                         * if (fcb_in->common->use_count == 1) {
                            const click_tcp *tcph = p->tcp_header();
                            const click_ip *iph = p->ip_header();
                            initializeFcbSyn(fcb_in, iph, tcph);
                        }*/
                        common->lock.release();
                        goto doopt;
                    } else {
                        if (unlikely(_verbose > 1))
                            print_packet("Renewing a socket !", fcb_in, p);


                        fcb_in->common->lock.release();

                        /*
                         * Release this side
                         * It may seem stupid to do reelease then reattach if reuse is true, but it is quite complex
                         *  to extract the release/renew state
                         */

                        SFCB_STACK(
                                release_tcp_internal(fcb_save); //Works because lock is reentrant
                        );


                        keep_fct = true;
                        goto eagain;
                    }


                } else { //state != closed (and packet is syn)

                    //If the connection is in state 1, it may be a new SYN ack
                    if((!isAck(p) || fcb_in->common->state >= TCPState::OPEN) && !checkRetransmission(fcb_in, p, false)) {
                        if (unlikely(_verbose))
                            print_packet("Retransmited SYN ack", fcb_in, p);
                        return 0;
                    }

                    /** We may be the SYN/ACK of a reused connection */
                    if (isAck(p)) {
                        fcb_in->common->lock.acquire();
                        if (unlikely(_verbose)) {
                                print_packet("SYN ack reinit", fcb_in, p);
                        }
                        if (fcb_in->common->use_count == 2) { //Still valid on the other side
                            SFCB_STACK(
                                    releaseFcbSide(fcb_save, fcb_in);
                            );
                            initializeFcbSide(fcb_in, p, true);
                            fcb_in->common->lock.release();
                            //Finish SYN/ACK initialization
                            goto doopt;
                        } else
                            fcb_in->common->lock.release();

                    }

                    //Not sure from which standard, but some hosts retry a SYN with a slightly increase sequence number.
                    if (fcb_in->common->state < TCPState::OPEN) {
                        goto reinitforward;
                    }

                    if (unlikely(_verbose)) {
                        click_chatter("Warning: Unexpected SYN packet (state %d, is_ack : %d, src %s:%d dst %s:%d). Dropping it",fcb_in->common->state, isAck(p),
                                IPAddress(p->ip_header()->ip_src).unparse().c_str(), ntohs(p->tcp_header()->th_sport),
                                IPAddress(p->ip_header()->ip_dst).unparse().c_str(), ntohs(p->tcp_header()->th_dport));

                    }
                    p->kill();
                    return NULL;
                }
            } else if (isAck(p) && fcb_in->common->state < TCPState::OPEN) {
				if (unlikely(_verbose > 1))
                        click_chatter("Warning: Unexpected non-SYN but ACK packet (state %d, is_ack : %d, is_fin : %d, src %s:%d dst %s:%d). Dropping it",fcb_in->common->state, isAck(p), isFin(p),
                                IPAddress(p->ip_header()->ip_src).unparse().c_str(), ntohs(p->tcp_header()->th_sport),
                                IPAddress(p->ip_header()->ip_dst).unparse().c_str(), ntohs(p->tcp_header()->th_dport));
				p->kill();
				return 0;
            } else if (fcb_in->common->state == TCPState::BEING_CLOSED_ARTIFICIALLY_2 && isFin(p)) {
                if (unlikely(_verbose)) {
                    click_chatter("Processing last artificial fin");
                }
                WritablePacket* packet = p->uniqueify();

                tcp_seq_t ackNumber = getAckNumber(packet);
                tcp_seq_t seqNumber = getSequenceNumber(packet);

                {
                    //First, respond an ACK to the sender
                    uint32_t saddr = getDestinationAddress(packet);
                    uint32_t daddr = getSourceAddress(packet);
                    uint16_t sport = getDestinationPort(packet);
                    uint16_t dport = getSourcePort(packet);

                    // The ACK is the sequence number sent by the source
                    // to which we add the size of the payload in order to acknowledge it
                    tcp_seq_t ackOf = getSequenceNumber(packet) + getPayloadLength(packet) + 1;

                    fcb_in->common->lock.acquire();
                    // Craft and send the ack
                    Packet* forged = outElement->forgeAck(fcb_in->common->maintainers[getOppositeFlowDirection()], saddr, daddr,
                            sport, dport, ackNumber, ackOf, true);
                    fcb_in->common->lock.release();
                    if (forged)
                        outElement->sendOpposite(forged);
                }

                //Second, change this packet to remove the fin

                // Map the ack number according to the ByteStreamMaintainer of the other direction
                if (allowResize()) {
                    fcb_in->common->lock.acquire();
                    ByteStreamMaintainer &maintainer = fcb_in->common->maintainers[getFlowDirection()];
                    ByteStreamMaintainer &otherMaintainer = fcb_in->common->maintainers[getOppositeFlowDirection()];
                    ackNumber = otherMaintainer.mapAck(ackNumber);
                    seqNumber = maintainer.mapSeq(seqNumber);
                    fcb_in->common->lock.release();
                }

                setAckNumber(packet, ackNumber);
                setSequenceNumber(packet, seqNumber + 1);
                click_tcp *tcph = packet->tcp_header();
                tcph->th_flags &= ~TH_FIN;
                outElement->sendModifiedPacket(packet);

                fcb_in->common->state = TCPState::CLOSED;
                return 0;
            }

        }

        tcp_seq_t currentSeq = getSequenceNumber(p);
        if (_reorder && currentSeq != fcb_in->expectedPacketSeq) {
            if (unlikely(_verbose))
                click_chatter("Flow is unordered... Awaiting %lu, have %lu", fcb_in->expectedPacketSeq, currentSeq);

            if (isRst(p)) { //If it's a RST, we process it even ooo
                if (unlikely(_verbose)) {
                    click_chatter("Rst out of order !");
                }
                goto force_process_packet;
            }

            //If packet is retransmission, we send it to the retransmitter
            if(!checkRetransmission(fcb_in, p, false)) {
                return 0;
            }

            if (_proactive_dup) {
                //This needs more enginnering, not retransmit too much, and only after processing all of the input maybe
                tcp_seq_t ack = fcb_in->expectedPacketSeq;
                /*                tcp_seq_t last_ack = fcb_in->common->maintainers[getOppositeFlowDirection()].getLastAckSent();
                if (SEQ_GT(ack, last_ack))
                    ack = last_ack;*/
                if (unlikely(_verbose))
                    click_chatter("Sending proactive DUP ACK for %lu",ack);

                // Get the information needed to ack the given packet
                uint32_t saddr = getDestinationAddress(p);
                uint32_t daddr = getSourceAddress(p);
                uint16_t sport = getDestinationPort(p);
                uint16_t dport = getSourcePort(p);
                // The SEQ value is the initial ACK value in the packet sent
                // by the source.
                tcp_seq_t seq;
                if (allowResize()) {
                    seq = getInitialAck(p);
                } else {
                    seq = getAckNumber(p);
                }

                fcb_in->common->lock.acquire();
                // Craft and send the ack
                click_chatter("Forging ack for proactive dup");
                Packet* forged = outElement->forgeAck(fcb_in->common->maintainers[getOppositeFlowDirection()], saddr, daddr,
                        sport, dport, seq, ack, true); //We force the sending as we want DUP ack on purpose
                fcb_in->common->lock.release();
                if (forged)
                    outElement->sendOpposite(forged);
            }

            putPacketInList(fcb_in, p);
            return 0;
        }
        force_process_packet:
        return processOrderedTCP(fcb_in, p);
    };

    EXECUTE_FOR_EACH_PACKET_DROPPABLE(fnt, flow, (void));

    //Out of order packets
    if (fcb_in->packetList) {
        BATCH_CREATE_INIT(nowOrderBatch);
        while (fcb_in->packetList)
        {
            if (getSequenceNumber(fcb_in->packetList) != fcb_in->expectedPacketSeq)
                break;

            if (unlikely(_verbose))
                click_chatter("Now in order %u %u !", getSequenceNumber(fcb_in->packetList), fcb_in->expectedPacketSeq);
            Packet* p = fcb_in->packetList;
            fcb_in->packetList = p->next();
            BATCH_CREATE_APPEND(nowOrderBatch, p);
            fcb_in->expectedPacketSeq = getNextSequenceNumber(p);
        }

        if (nowOrderBatch) {
            BATCH_CREATE_FINISH(nowOrderBatch);
            if (unlikely(_verbose > 1))
                click_chatter("Now order : %p %d",nowOrderBatch,nowOrderBatch->count());
            fcb_acquire(nowOrderBatch->count());
            auto refnt = [this,fcb_in](Packet* p){return processOrderedTCP(fcb_in,p);};
            EXECUTE_FOR_EACH_PACKET_DROPPABLE(refnt, nowOrderBatch, (void));
            if (flow && nowOrderBatch) {
#if DEBUG_TCP
                click_chatter("Flow : %p %d",flow,flow->count());
#endif
                flow->append_batch(nowOrderBatch);
            } else {
#if DEBUG_TCP
                click_chatter("Processing now ordered!");
#endif
                flow = nowOrderBatch;
            }
        }
    }

    if (flow)
        output(0).push_batch(flow);

    //Release FCB if we are now closing
    if (fcb_in->common) {
        TCPState::Value state = fcb_in->common->state; //Read-only for fast path
        int _timewait = 1; //As a middlebox, we have to keep the block for _timewait
        if (( _timewait == 0 && (state == TCPState::BEING_CLOSED_GRACEFUL_2 ||
                state == TCPState::CLOSED))) {
            releaseFCBState();
        }
    }
    //click_chatter("END Fcb in : %p, Common : %p, Batch : %p, State %d", fcb_in, fcb_in->common,flow,(fcb_in->common?fcb_in->common->state:-1));

}

int TCPIn::tcp_initialize(ErrorHandler *errh) {
    if (get_passing_threads(true).weight() <= 1) {
        tableFcbTcpCommon.disable_mt();
    }
    _modification_level = outElement->maxModificationLevel(0);
    if (this->returnElement->getFlowDirection() != 1 - getFlowDirection()) {
        return errh->error("Bad flow direction between %p{element}=%d and %p{element}=%d",this,this->returnElement,getFlowDirection(),this->returnElement->getFlowDirection());
    }
    return 0;
}

TCPOut* TCPIn::getOutElement()
{
    return outElement;
}

TCPIn* TCPIn::getReturnElement()
{
    return returnElement;
}

void TCPIn::resetReorderer(struct fcb_tcpin* tcpreorder) {
    SFCB_STACK( //Packet in the list have no reference
            FOR_EACH_PACKET_LL_SAFE(tcpreorder->packetList,p) {
        if (unlikely(_verbose))
            click_chatter("WARNING : Non-free TCPReorder flow bucket , seq %lu, expected %lu",getSequenceNumber(p),tcpreorder->expectedPacketSeq);
            p->kill();
    }
    );
    tcpreorder->packetList = 0;
    tcpreorder->packetListLength = 0;
    tcpreorder->expectedPacketSeq = 0;
}

void TCPIn::release_tcp_internal(FlowControlBlock* fcb) {
    auto fcb_in = fcb_data_for(fcb);
    auto &common = fcb_in->common;
    releaseFcbSide(fcb, fcb_in);
    if (common) {
        common->lock.acquire();
        //The last one release common
        if (--common->use_count == 0) {
            common->lock.release();
            //lock->acquire();
            if (common->state < TCPState::OPEN) { //common still in the connection table
                //click_chatter("Delay release %p", common);
            } else {
                //click_chatter("Direct release %p", common);
                poolFcbTcpCommon.release(common); //Will call ~common
            }
            //lock->release();
        }
        else {

            //click_chatter("Unref %p", common);
            common->lock.release();
        }

        fcb_in->common = 0;
    } else {
        click_chatter("Double release !");
    }

}


void TCPIn::release_tcp(FlowControlBlock* fcb, void* thunk) {
    TCPIn* tin = static_cast<TCPIn*>(thunk);
    auto fcb_in = tin->fcb_data_for(fcb);

    tin->release_tcp_internal(fcb);

#if HAVE_FLOW_DYNAMIC
    if (fcb_in->previous_fnt) {
        flow_assert(fcb_in->previous_fnt != &release_tcp);
        fcb_in->previous_fnt(fcb, fcb_in->previous_thunk);
    }
#endif
}

/**
 * Remove timeout and release fct. Common is destroyed but
 *  the FCB stays up until all packets are freed.
 */
void TCPIn::releaseFCBState() {
    //click_chatter("TCP is closing, killing state");
    ctx_release_timeout();
#if HAVE_FLOW_DYNAMIC
    fcb_remove_release_fnt(fcb_data(), &release_tcp);
#endif
    SFCB_STACK(
            release_tcp_internal(fcb_save);
    );
}

void TCPIn::closeConnection(Packet *packet, bool graceful)
{
    auto fcb_in = fcb_data();
    uint8_t newFlag = 0;

    if(graceful)
        newFlag = TH_FIN;
    else
        newFlag = TH_RST;

    click_tcp tcph = *packet->tcp_header();

    // Change the flags of the packet
    tcph.th_flags = tcph.th_flags | newFlag;

    TCPState::Value newState;

    fcb_in->common->lock.acquire();
    if(!graceful)
    {
        newState = TCPState::CLOSED;
    } else {
        newState = TCPState::BEING_CLOSED_ARTIFICIALLY_1;
    }
    fcb_in->common->state = newState;
    fcb_in->common->lock.release();

    //Close the forward side if graceful
    if(graceful) {
        // Get the information needed to ack the given packet
        uint32_t daddr = getDestinationAddress(packet);
        uint32_t saddr = getSourceAddress(packet);
        uint16_t dport = getDestinationPort(packet);
        uint16_t sport = getSourcePort(packet);

        // Craft and send the ack
        outElement->sendClosingPacket(fcb_in->common->maintainers[getFlowDirection()],
                saddr, daddr, sport, dport, graceful);
    } else {
        //Send RST to other side

        // Get the information needed to ack the given packet
        uint32_t saddr = getDestinationAddress(packet);
        uint32_t daddr = getSourceAddress(packet);
        uint16_t sport = getDestinationPort(packet);
        uint16_t dport = getSourcePort(packet);

        // Craft and send the RST
        outElement->sendClosingPacket(fcb_in->common->maintainers[getFlowDirection()],
                daddr, saddr, dport, sport, graceful);
        // Craft and send the RST
        returnElement->outElement->sendClosingPacket(fcb_in->common->maintainers[getOppositeFlowDirection()],
                saddr, daddr, sport, dport, graceful);
        click_chatter("Ungracefull close, releasing FCB state");
        releaseFCBState();
    }

    CTXElement::closeConnection(packet, graceful);
}

ModificationList* TCPIn::getModificationList(WritablePacket* packet)
{
    auto fcb_in = fcb_data();
    auto modificationLists = fcb_in->modificationLists;
    if (modificationLists == 0) {
        modificationLists = poolModificationTracker.allocate();
        fcb_in->modificationLists = modificationLists;
    }


    ModificationList* list = NULL;

    // Search the modification list in the hashtable
    ModificationTracker::iterator it =
            modificationLists->find(getSequenceNumber(packet));

    // If we could find the element
    if(it != modificationLists->end())
        list = it.value();

    // If no list was assigned to this packet, create a new one
    if(list == NULL)
    {
        ModificationList* listPtr = poolModificationLists->getMemory();
        // Call the constructor manually to have a clean object
        list = new(listPtr) ModificationList(&(*poolModificationNodes));
        modificationLists->set(getSequenceNumber(packet), list);
    }

    return list;
}

bool TCPIn::hasModificationList(Packet* packet)
{
    auto fcb_in = fcb_data();
    auto modificationLists = fcb_in->modificationLists;
    if (!fcb_in->modificationLists)
        return false;
    ModificationTracker::iterator it =
            modificationLists->find(getSequenceNumber(packet));

    return (it != modificationLists->end());
}

void TCPIn::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    ModificationList* list = getModificationList(packet);

    tcp_seq_t seqNumber = getSequenceNumber(packet);

    // Used to have the position in the TCP flow and not in the packet
    uint16_t tcpOffset = getPayloadOffset(packet);

    uint16_t contentOffset = packet->getContentOffset();
    if (unlikely(_verbose))
        click_chatter("Adding modification at seq %lu, pos in content %lu, seq+pos %lu, removing %d bytes",seqNumber, position, seqNumber + (position + contentOffset) - tcpOffset, length);
    list->addModification(seqNumber, seqNumber + (position + contentOffset) - tcpOffset, -((int)length));

    if(position + contentOffset > packet->length())
    {
        click_chatter("Error: Invalid removeBytes call (packet length: %u, position: %u)",
                packet->length(), position);
        return;
    }


    // Continue in the stack function
    CTXElement::removeBytes(packet, position, length);
}

WritablePacket* TCPIn::insertBytes(WritablePacket* packet, uint32_t position,
        uint32_t length)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);

    uint16_t tcpOffset = getPayloadOffset(packet);
    uint16_t contentOffset = packet->getContentOffset();
    if (unlikely(_verbose))
        click_chatter("Adding modification at seq %lu, pos in content %lu, seq+pos %lu, adding %d bytes",seqNumber, position, seqNumber + ((position + contentOffset) - tcpOffset), length);
    getModificationList(packet)->addModification(seqNumber, seqNumber + (position + contentOffset - tcpOffset),
            (int)length);

    return CTXElement::insertBytes(packet, position, length);
}

void TCPIn::requestMorePackets(Packet *packet, bool force)
{
    if (!fcb_data()->common || fcb_data()->common->state == TCPState::CLOSED) { //Connection is closed
        click_chatter("WARNING: Requesting more packets for a closed connection");
        return;
    }
    //click_chatter("TCP requestMorePackets");
    ackPacket(packet, force);

    // Continue in the stack function
    CTXElement::requestMorePackets(packet, force);
}

void TCPIn::ackPacket(Packet* packet, bool force)
{
    auto fcb_in = fcb_data();
    // Get the information needed to ack the given packet
    uint32_t saddr = getDestinationAddress(packet);
    uint32_t daddr = getSourceAddress(packet);
    uint16_t sport = getDestinationPort(packet);
    uint16_t dport = getSourcePort(packet);
    // The SEQ value is the initial ACK value in the packet sent
    // by the source.
    tcp_seq_t seq;
    if (allowResize()) {
        seq = getInitialAck(packet);
    } else {
        seq = getAckNumber(packet);
    }

    // The ACK is the sequence number sent by the source
    // to which we add the size of the payload in order to acknowledge it
    tcp_seq_t ack = getSequenceNumber(packet) + getPayloadLength(packet);

    if(isFin(packet) || isSyn(packet))
        ack++;

    fcb_in->common->lock.acquire();
    ByteStreamMaintainer &maintainer = fcb_in->common->maintainers[getOppositeFlowDirection()];
    // Craft and send the ack
    Packet* forged = outElement->forgeAck(maintainer, saddr, daddr,
            sport, dport, seq, ack, force);
    fcb_in->common->lock.release();
    if (forged)
        outElement->sendOpposite(forged);
}

bool TCPIn::checkConnectionClosed(Packet *packet)
{
    auto fcb_in = fcb_data();

    TCPState::Value state = fcb_in->common->state; //Read-only access, no need to lock

    if (unlikely(_verbose)) {
        if (_verbose > 2)
            click_chatter("Connection state is %d", state);
    }
    // If the connection is open, we just check if the packet is a FIN. If it is we go to the hard sequence.
    if (likely(state == TCPState::OPEN))
    {
        if (isFin(packet) || isRst(packet)) {
            //click_chatter("Connection is closing, we received a FIN or RST in open state");
            goto do_check;
        }
        return false;
    } else if (state < TCPState::OPEN) {
        if (isRst(packet)) {
            goto do_check;
        }
        return false;
    }

    do_check: //Packet is Fin or Rst
    fcb_in->common->lock.acquire(); //Re read with lock if not in fast path
    state = fcb_in->common->state;

    if (isRst(packet)) {
        if (unlikely(_verbose > 2))
            click_chatter("RST received, connection is now closed");
        fcb_in->common->state = TCPState::CLOSED;
        fcb_in->common->lock.release();
        resetReorderer(fcb_in);
        return false;
    }

    if(state == TCPState::OPEN) {
        if (unlikely(_verbose > 2))
            click_chatter("TCP is now closing with the first FIN");
        fcb_in->fin_seen = true;
        fcb_in->common->state = TCPState::BEING_CLOSED_GRACEFUL_1;
        fcb_in->common->lock.release();
        return false; //Let the FIN through. We cannot release now as there is an ACK that needs to come
        // If the connection is being closed and we have received the last packet, close it completely
    }
    else if (unlikely(state == TCPState::BEING_CLOSED_ARTIFICIALLY_1))
    {
        if(isFin(packet)) {
            if (unlikely(_verbose > 1))
                click_chatter("Connection is being closed gracefully artificially by us, this is the second FIN. Changing ACK to -1.");

            tcp_seq_t ackNumber = getAckNumber(packet);
            setAckNumber((WritablePacket*)packet, ackNumber-1);
            //Todo : stay in this state until the ACK comes back
            fcb_in->common->state = TCPState::BEING_CLOSED_ARTIFICIALLY_2;
        } else {
            if (unlikely(_verbose > 1))
                click_chatter("Connection is being closed gracefully artificially by other side, this is a normal ACK");
        }
        fcb_in->common->lock.release();
        return false; //Let the packet through anyway
    }
    else if (unlikely(state == TCPState::BEING_CLOSED_ARTIFICIALLY_2))
    {
        return false; //It's not a FIN, let ACKs go through.
    }
    else if(state == TCPState::BEING_CLOSED_GRACEFUL_1)
    {
        if(isFin(packet) && !fcb_in->fin_seen) {
            if (unlikely(_verbose > 2))
                click_chatter("Connection is being closed gracefully, this is the second FIN");
            fcb_in->common->state = TCPState::BEING_CLOSED_GRACEFUL_2;
            fcb_in->fin_seen = true;
        } else {
            if (unlikely(_verbose > 2))
                click_chatter("FCB already seen on this side");
        }
        fcb_in->common->lock.release();
        return false; //Let the packet through anyway
    }
    else if(state == TCPState::BEING_CLOSED_GRACEFUL_2)
    {
        if (unlikely(_verbose > 2))
            click_chatter("Connection is being closed gracefully, this is the last ACK");
        fcb_in->common->state = TCPState::CLOSED;
        fcb_in->common->lock.release();
        return false; //We need the out element to eventually correct the ACK number
    } else if(state == TCPState::CLOSED) {
        if (isJustAnAck(packet, true)) {
            //Probable retransmission of the last ack
            fcb_in->common->lock.release();
            return false;
        }
    } else {
        if (unlikely(_verbose)) {
            click_chatter("Unhandled state %d", fcb_in->common->state);
        }
    }

    fcb_in->common->lock.release();
    return true;
}

unsigned int TCPIn::determineFlowDirection()
{
    return getFlowDirection();
}

inline void TCPIn::initializeFcbSide(fcb_tcpin* fcb_in, Packet* packet, bool keep_fct) {
    fcb_in->fin_seen = false;

    if (allowResize() || returnElement->allowResize()) {
        // Initialize the RBT with the RBTManager
        if (_verbose > 2)
            click_chatter("Initialize direction %d for SYN/ACK",getFlowDirection());
        // The data in the flow will start at current sequence number
        uint32_t flowStart = getSequenceNumber(packet);

        fcb_in->common->maintainers[getFlowDirection()].initialize(&(*rbtManager), flowStart);
    }

    fcb_in->expectedPacketSeq = getSequenceNumber(packet); //Not next because this one will be checked just after

    if (!keep_fct) {
        ctx_acquire_timeout(TCP_TIMEOUT);
#if HAVE_FLOW_DYNAMIC
        fcb_set_release_fnt(fcb_in, release_tcp);
#endif
    }

    // Set information about the flow
    fcb_in->common->maintainers[getFlowDirection()].setIpSrc(getSourceAddress(packet));
    fcb_in->common->maintainers[getFlowDirection()].setIpDst(getDestinationAddress(packet));
    fcb_in->common->maintainers[getFlowDirection()].setPortSrc(getSourcePort(packet));
    fcb_in->common->maintainers[getFlowDirection()].setPortDst(getDestinationPort(packet));
}


inline void TCPIn::releaseFcbSide(FlowControlBlock* fcb, fcb_tcpin* fcb_in) {
    fcb_in->fin_seen = false;

#if HAVE_FLOW_DYNAMIC
    if (fcb_in->conn_release_fnt) {
        fcb_in->conn_release_fnt(fcb,fcb_in->conn_release_thunk);
        fcb_in->conn_release_fnt = 0;
    }
#endif
    resetReorderer(fcb_in);
    if (fcb_in->modificationLists) {
        for(HashTable<tcp_seq_t, ModificationList*>::iterator it = fcb_in->modificationLists->begin();
                it != fcb_in->modificationLists->end(); ++it)
        {
            // Call the destructor to release the object's own memory
            (it.value())->~ModificationList();
            // Put it back in the pool
            poolModificationLists->releaseMemory(it.value());
        }
        poolModificationTracker.release(fcb_in->modificationLists);
        fcb_in->modificationLists = 0;
    }
}


inline void TCPIn::initializeFcbSyn(fcb_tcpin* fcb_in, const click_ip *iph , const click_tcp *tcph ) {

    IPFlowID flowID(iph->ip_src, tcph->th_sport, iph->ip_dst, tcph->th_dport);
    //A pending reset or time out connection could exist so we use replace and free any existing entry if found
    tableFcbTcpCommon.insert(flowID, fcb_in->common, [this](tcp_common* &existing) {
        existing->lock.acquire();
        if (--existing->use_count == 0) {
            existing->lock.release();
            poolFcbTcpCommon.release(existing);
        } else {
            click_chatter("Probable bug 828");
            existing->lock.release();
        }
    });
}

bool TCPIn::registerConnectionClose(StackReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt, void* thunk)
{
    auto fcb_in = fcb_data();
#if HAVE_FLOW_DYNAMIC
    fcb_chain->previous_fnt = fcb_in->conn_release_fnt;
    fcb_chain->previous_thunk = fcb_in->conn_release_thunk;
    fcb_in->conn_release_fnt = fnt;
    fcb_in->conn_release_thunk = thunk;
#endif
    return true;
}



bool TCPIn::assignTCPCommon(Packet *packet, bool keep_fct)
{
    auto fcb_in = fcb_data();

    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;
    const click_ip *iph = packet->ip_header();

    // Check if we are the side initiating the connection or not
    // (if ACK flag, we are not the initiator)
    if(((flags & TH_ACK && flags & TH_SYN)) || flags & TH_RST)
    {
        //click_chatter("SynAck or RST"); //For both we need the matching connection, for RST to close it if any

        // Get the flow ID for the opposite side of the connection
        IPFlowID flowID(iph->ip_dst, tcph->th_dport, iph->ip_src, tcph->th_sport);

        // Get the struct allocated by the initiator, and remove it if found
        fcb_in->common = returnElement->getTCPCommon(flowID);

        if (fcb_in->common == 0) //No matching connection
            return false;

        //No need to fcb_in->common->use_count++, we keep the reference that belonged to the table

        //click_chatter("Found %p", fcb_in->common);
        //assert(fcb_in->common->state == TCPState::ESTABLISHING_1 || fcb_in->common->state == TCPState::ESTABLISHING_2);

        if (flags & TH_RST) {
            click_chatter("Reset");
            fcb_in->common->state = TCPState::CLOSED;
            fcb_in->common->lock.acquire();
            fcb_in->common->use_count--;
            fcb_in->common->lock.release();
            fcb_in->common = 0;
            //We have no choice here but to rely on the other side timing out, this is better than traversing the tree of the other side. When waking up,
            //it will see that the state is being closed ungracefull and need to cleans
            return false; //Note that RST will be catched on return and will still go through, as the dest needs to know the flow is rst
        }

        initializeFcbSide(fcb_in, packet, keep_fct);
        //click_chatter("RE Common is %p",fcb_in->common);
    }
    else
    {
        if(!(flags & TH_SYN)) {//First packet, not rst and not syn... Discard
            if (_verbose > 1)
                click_chatter("Not syn !");
            return false;
        }


        // We are the initiator, so we need to allocate memory
        fcb_in->common = poolFcbTcpCommon.allocate();
        //click_chatter("Alloc %p", allocated);
        //assert(!allocated->maintainers[0].initialized);
        //assert(!allocated->maintainers[1].initialized);

        fcb_in->common->use_count = 2; //One for us, one for the table

        //DEBUG
        //        if (*tableFcbTcpCommon.find(flowID) != allocated) {
        //            allocated = *tableFcbTcpCommon.find(flowID);
        //            click_chatter("Looking for that flow gave %p, in state %d, uc %d. It is not bad because it should be released before us.", allocated, allocated->state, allocated->use_count);
        //            assert(false);
        //        }



        // Set the pointer in the structure
        initializeFcbSide(fcb_in, packet, keep_fct);

        if (getFlowDirection() == 1)
            fcb_in->common->state = TCPState::ESTABLISHING_1;
        else
            fcb_in->common->state = TCPState::ESTABLISHING_2;

        initializeFcbSyn(fcb_in, iph, tcph);



        // Store in our structure the information needed to free the memory
        // of the common structure
        //fcb_in->flowID = flowID;

        //click_chatter("AL Common is %p",fcb_in->common);
    }


    return true;
}

bool TCPIn::isEstablished()
{
    auto fcb_in = fcb_data();
    return fcb_in->common && fcb_in->common->state > TCPState::ESTABLISHING_2;
}

bool TCPIn::isLastUsefulPacket(Packet *packet)
{
    if(isFin(packet) || isRst(packet))
        return true;
    else
        return false;
}

tcp_common* TCPIn::getTCPCommon(IPFlowID flowID)
{
    tcp_common* p;
    bool it = tableFcbTcpCommon.find_erase_clean(flowID,[&p](tcp_common* &c){p=c;},[this](tcp_common* &c){
        if (unlikely(c->state >= TCPState::OPEN)) { //An established connection in the list : means a pending connection was reset
            c->lock.acquire();
            if (unlikely(c->state < TCPState::OPEN)) {
                click_chatter("Concurrent access... This is not a dangerous bug, but please report bug 978.");
            }
            if (likely(--c->use_count == 0)) { //The reseter left a reference, it is likely we have the last (or it would not be pending)
                c->lock.release();
                poolFcbTcpCommon.release(c); //Will call ~common
                return true;
            } else {
                click_chatter("BUG : established connection (state %d) with %d references in list", c->state, c->use_count + 1);
                c->lock.release();
                return true;
            }
        } else if (unlikely(c->use_count == 1)) { //We have the only reference -> the inserter released it
            c->lock.acquire();
            if (likely(--c->use_count == 0)) { //Normally it means we have the only reference, so nobody could have grabed the lock
                c->lock.release();
                click_chatter("Release delayed %p", c);
                poolFcbTcpCommon.release(c); //Will call ~common
            } else {
                c->lock.release();
                //If we have the only reference, this test should always succeed
                click_chatter("Concurreny. Please report because this case should be impossible.");
            }
            return true;
        }
        return false;
    });

    if(!it)
    {
        return NULL; // Not in the table
    }
    else
    {
        return p;
    }
}

void TCPIn::manageOptions(WritablePacket *packet)
{
    auto fcb_in = fcb_data();

    click_tcp *tcph = packet->tcp_header();

    uint8_t *optStart = (uint8_t *) (tcph + 1);
    uint8_t *optEnd = (uint8_t *) tcph + (tcph->th_off << 2);

    if(optEnd > packet->end_data())
        optEnd = packet->end_data();

    while(optStart < optEnd)
    {
        if(optStart[0] == TCPOPT_EOL) // End of list
            break; // Stop searching
        else if(optStart[0] == TCPOPT_NOP)
            optStart += 1; // Move to the next option
        else if(optStart[1] < 2 || optStart[1] + optStart > optEnd)
            break; // Avoid malformed options
        else if(optStart[0] == TCPOPT_SACK_PERMITTED && optStart[1] == TCPOLEN_SACK_PERMITTED)
        {
            uint32_t old_hw = *((uint16_t*)optStart);
            // If we find the SACK permitted option, we remove it
            for(int i = 0; i < TCPOLEN_SACK_PERMITTED; ++i) {
                optStart[i] = TCPOPT_NOP; // Replace the option with NOP
            }
            uint32_t new_hw = (TCPOPT_NOP << 8) + TCPOPT_NOP;

            click_update_in_cksum(&tcph->th_sum, old_hw, new_hw);
            //click_chatter("SACK Permitted removed from options");

            optStart += optStart[1];
        }
        else if(optStart[0] == TCPOPT_WSCALE && optStart[1] == TCPOLEN_WSCALE)
        {
            if (allowResize()) {
                uint16_t winScale = optStart[2];

                if(winScale >= 1)
                    winScale = 2 << (winScale - 1);

                fcb_in->common->maintainers[flowDirection].setWindowScale(winScale);
                fcb_in->common->maintainers[flowDirection].setUseWindowScale(true);

                //click_chatter("Window scaling set to %u for flow %u", winScale, flowDirection);

                if(isAck(packet))
                {
                    // Here, we have a SYNACK
                    // It means that we know if the other side of the flow
                    // has the option enabled
                    // if this is not the case, we disable it for this side as well
                    if(!fcb_in->common->maintainers[getOppositeFlowDirection()].getUseWindowScale())
                    {
                        fcb_in->common->maintainers[flowDirection].setUseWindowScale(false);
                        //click_chatter("Window scaling disabled");
                    }
                }
            }
            optStart += optStart[1];
        }
        else if(optStart[0] == TCPOPT_MAXSEG && optStart[1] == TCPOLEN_MAXSEG)
        {

            if (allowResize()) {
                uint16_t mss = (optStart[2] << 8) | optStart[3];
                fcb_in->common->maintainers[flowDirection].setMSS(mss);

                //click_chatter("MSS for flow %u: %u", flowDirection, mss);
                fcb_in->common->maintainers[flowDirection].setCongestionWindowSize(mss);
            }

            optStart += optStart[1];
        }
        else
            optStart += optStart[1]; // Move to the next option
    }
}

void TCPIn::setFlowDirection(unsigned int flowDirection)
{
    this->flowDirection = flowDirection;
}

pool_allocator_mt<tcp_common,true,TCPCOMMON_POOL_SIZE> TCPIn::poolFcbTcpCommon;
pool_allocator_mt<ModificationTracker,false> TCPIn::poolModificationTracker;



CLICK_ENDDECLS
EXPORT_ELEMENT(TCPIn)
ELEMENT_MT_SAFE(TCPIn)
