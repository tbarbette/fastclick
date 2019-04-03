/*
 * tcpretransmitter.{cc,hh} -- manages the tcp retransmissions and ensures that the data we are
 * responsible for (data we ACKed) are correctly received and that their transmission
 * is done correctly, using the tcp mechanisms such as slow start.
 * Romain Gaillard
 *
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include "tcpretransmitter.hh"


TCPRetransmitter::TCPRetransmitter()
{
    // Initialize the memory pools and buffer of each thread
    for(unsigned int i = 0; i < circularPool.weight(); ++i)
        circularPool.get_value(i).initialize(TCPRETRANSMITTER_BUFFER_NUMBER);

    for(unsigned int i = 0; i < getBuffer.weight(); ++i)
        getBuffer.get_value(i).resize(TCPRETRANSMITTER_GET_BUFFER_SIZE, '\0');

    for(unsigned int i = 0; i < rawBufferPool.weight(); ++i)
        rawBufferPool.get_value(i) = NULL;
}

TCPRetransmitter::~TCPRetransmitter()
{
    for(unsigned int i = 0; i < rawBufferPool.weight(); ++i)
    {
        if(rawBufferPool.get_value(i) != NULL)
            delete rawBufferPool.get_value(i);
    }
}

int TCPRetransmitter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    uint32_t initialBufferSize = 65535;

    if(Args(conf, this, errh)
    .read_p("INITIALBUFFERSIZE", initialBufferSize)
    .complete() < 0)
        return -1;

    for(unsigned int i = 0; i < rawBufferPool.weight(); ++i)
        rawBufferPool.get_value(i) = new BufferPool(TCPRETRANSMITTER_BUFFER_NUMBER, initialBufferSize);

    ElementCastTracker visitor(router(), "TCPIn");
    router()->visit_upstream(this,0,&visitor);
    if (visitor.size() != 1) {
        return errh->error("Could not find TCPIn element !");
    } else {
        _in = static_cast<TCPIn*>(visitor[0]);
    }
    return 0;
}


int TCPRetransmitter::initialize(ErrorHandler *errh) {
    return 0;
}

void TCPRetransmitter::push_batch(int port, PacketBatch *batch)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(port == 0)
        EXECUTE_FOR_EACH_PACKET_DROPPABLE(processPacketNormal, batch, [](Packet* p){})
    else
        EXECUTE_FOR_EACH_PACKET_DROPPABLE(processPacketRetransmission, batch, [](Packet* p){})

    if(batch != NULL)
        output_push_batch(0, batch);
}


void TCPRetransmitter::checkInitialization()
{
    auto fcb = _in->fcb_data();
    unsigned int flowDirection = determineFlowDirection();

    fcb->common->lock.acquire();

    RetransmissionTiming &manager = fcb->common->retransmissionTimings[flowDirection];

    // If timer has not been initialized yet, do it
    if(!manager.isTimerInitialized())
        manager.initTimer(this);

    // If this had not been done previously, assign a circular buffer
    // for this flow
    if(manager.getCircularBuffer() == NULL)
    {
        CircularBuffer *circularBuffer = (*circularPool).getMemory();
        // Call the constructor of CircularBuffer giving it the pool of raw buffers
        circularBuffer = new(circularBuffer) CircularBuffer(*rawBufferPool);
        manager.setCircularBuffer(circularBuffer, &(*circularPool));
    }

    fcb->common->lock.release();
}

Packet* TCPRetransmitter::processPacketNormal(Packet *packet)
{
    auto fcb = _in->fcb_data();
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->common == NULL)
    {
        return packet;
    }

    fcb->common->lock.acquire();

    RetransmissionTiming &manager = fcb->common->retransmissionTimings[flowDirection];
    checkInitialization();
    CircularBuffer *buffer = manager.getCircularBuffer();
    ByteStreamMaintainer &maintainer = fcb->common->maintainers[flowDirection];

    // The packet comes on the normal input
    // We thus need to add its content in the buffer and let it continue
    const unsigned char* content = getPayloadConst(packet);
    uint32_t contentSize = getPayloadLength(packet);
    uint32_t seq = getSequenceNumber(packet);

    if(contentSize > 0)
    {
        // If this is the first time we add content in the buffer, we provide
        // the buffer with the sequence number
        // so that we will be able to give sequence numbers as indexes in
        // the future
        if(buffer->isBlank())
            buffer->setStartOffset(seq);

        // Add the content of the packet to the buffer
        buffer->addDataAtEnd(content, contentSize);

        uint32_t lastAckSent = 0;
        bool lastAckSentSet = fcb->common->maintainers[oppositeFlowDirection].isLastAckSentSet();
        if(lastAckSentSet)
            lastAckSent = fcb->common->maintainers[oppositeFlowDirection].getLastAckSent();

        // ackToReceive is the ACK that will be received for the current
        // packet. We map it to be able to compare it with lastAckSent
        // which is a mapped value
        uint32_t ackToReceive = seq + contentSize;
        if(isFin(packet) || isSyn(packet))
            ackToReceive++;
        uint32_t ackToReceiveMapped = fcb->common->maintainers[flowDirection].mapAck(ackToReceive);

        if(lastAckSentSet && SEQ_LEQ(ackToReceiveMapped, lastAckSent))
        {
            // We know that we are transmitting ACKed data
            // We thus start the retransmission timer
            fcb->common->retransmissionTimings[flowDirection].startTimer();

            uint16_t sizeOfTransmission = getMaxAmountData(getPayloadLength(packet), false);

            // We delay the transmission of these data
            if(sizeOfTransmission == 0)
            {
                packet->kill();
                fcb->common->lock.release();
                return NULL;
            }

            fcb->common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);
        }

        // Start a new RTT measure if possible
        fcb->common->retransmissionTimings[flowDirection].startRTTMeasure(seq);
    }

    fcb->common->lock.release();
    return packet;
}

Packet* TCPRetransmitter::processPacketRetransmission(Packet *packet)
{
    auto fcb = _in->fcb_data();
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->common == NULL)
    {
        //Do not retransmit packets of closed connection (TCPOut freed the common, or it was never properlu established)
        packet->kill();
        return NULL;
    }

    fcb->common->lock.acquire();

    RetransmissionTiming &manager = fcb->common->retransmissionTimings[flowDirection];
    checkInitialization();
    CircularBuffer *buffer = manager.getCircularBuffer();
    ByteStreamMaintainer &maintainer = fcb->common->maintainers[flowDirection];

    // The packet comes on the second input
    // It means we have a retransmission.
    // We need to perform the mapping between the received packet and
    // the data to retransmit from the tree

    // The given packet is "raw", meaning the sequence number and the ack
    // are unmodified. Thus, we need to perform the right mappings to
    // have the link with the packets in the tree

    if(fcb->common->state > TCPState::OPEN)
    {
        packet->kill();
        fcb->common->lock.release();
        return NULL;
    }

    // Get the sequence number that will be the key of the packet in the buffer
    uint32_t seq = getSequenceNumber(packet);

    uint32_t lastAckSent = fcb->common->maintainers[oppositeFlowDirection].getLastAckSent();

    if(SEQ_LT(seq, lastAckSent))
    {
        // We must check the lastAckSent of the other side
        // If we receive data already ACKed, we must drop them and make the other side
        // resend the last ACK, because it means that the ACK was lost.
        // (We sent an ACK and we receive a retransmission nevertheless)

        // Resend the ACK for this packet
        setInitialAck(packet, getAckNumber(packet));
        requestMorePackets(packet, true);
        packet->kill();
        fcb->common->lock.release();
        return NULL;
    }

    uint32_t mappedSeq = maintainer.mapSeq(seq);

    // Ensure that the sequence number is at least equal to the last ack received to avoid
    // sending useless data
    if(SEQ_LT(mappedSeq, fcb->common->getLastAckReceived(oppositeFlowDirection)))
        mappedSeq = fcb->common->getLastAckReceived(oppositeFlowDirection);

    uint32_t payloadSize = getPayloadLength(packet);
    uint32_t mappedSeqEnd = maintainer.mapSeq(seq + payloadSize);

    // The lower bound of the interval to retransmit is "mappedSeq"
    // The higher bound of the interval to retransmit is "mappedSeqEnd"
    // mappedSeqEnd is not just mappedSeq + payloadSize as it must take
    // into account the data removed inside the flow

    // Check if we really have something to retransmit
    // If the full content of the packet was removed, mappedSeqEnd = mappedSeq
    uint32_t sizeOfRetransmission = 0;

    if(SEQ_LT(mappedSeq, mappedSeqEnd))
        sizeOfRetransmission = mappedSeqEnd - mappedSeq;

    // If the packet is just a FIN or RST packet, we let it go as it is, with seq and ack mapped
    if(getPayloadLength(packet) == 0 && (isFin(packet) || isRst(packet)))
    {
        WritablePacket *newPacket = packet->uniqueify();

        // Map the previous ACK
        uint32_t ack = getAckNumber(newPacket);
        ack = fcb->common->maintainers[oppositeFlowDirection].mapAck(ack);
        setAckNumber(newPacket, ack);
        setSequenceNumber(newPacket, mappedSeq);

        // Recompute the checksums
        computeTCPChecksum(newPacket);
        computeIPChecksum(newPacket);

        fcb->common->lock.release();
        return newPacket;
    }

    if(sizeOfRetransmission == 0)
    {
        packet->kill();
        fcb->common->lock.release();
        return NULL;
    }

    // Get the data from the buffer
    manager.getCircularBuffer()->getData(mappedSeq, sizeOfRetransmission, *getBuffer);
    // The data to retransmit are now in "getBuffer"

    // Map the previous ACK
    uint32_t ack = getAckNumber(packet);
    ack = fcb->common->maintainers[oppositeFlowDirection].mapAck(ack);

    // Uniqueify the packet so we can modify it to replace its content
    WritablePacket *newPacket = packet->uniqueify();

    // Ensure that the packet has the right size
    uint32_t previousPayloadSize = getPayloadLength(newPacket);
    if(sizeOfRetransmission > previousPayloadSize)
        newPacket = newPacket->put(sizeOfRetransmission - previousPayloadSize);
    else if(sizeOfRetransmission < previousPayloadSize)
        newPacket->take(previousPayloadSize - sizeOfRetransmission);

    setAckNumber(newPacket, ack);
    setSequenceNumber(newPacket, mappedSeq);

    // Set the new size of the packet in the IP header (total size minus ethernet header)
    setPacketTotalLength(newPacket, newPacket->length() - getIPHeaderOffset(packet));

    // Copy the content of the buffer to the payload area
    setPayload(newPacket, &((*getBuffer)[0]), sizeOfRetransmission);

    // Recompute the checksums
    computeTCPChecksum(newPacket);
    computeIPChecksum(newPacket);

    // Signal the retransmission to avoid taking retransmitted packets
    // into account to compute the RTT
    fcb->common->retransmissionTimings[flowDirection].signalRetransmission(mappedSeq + payloadSize);

    fcb->common->lock.release();
    return newPacket;
}

void TCPRetransmitter::prune()
{
    auto fcb = _in->fcb_data();
    unsigned flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->common->lock.acquire();
    ByteStreamMaintainer &maintainer = fcb->common->maintainers[oppositeFlowDirection];

    CircularBuffer *buffer = fcb->common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
    {
        fcb->common->lock.release();
        return;
    }

    if(!fcb->common->lastAckReceivedSet())
    {
        fcb->common->lock.release();
        return;
    }

    // We remove ACKed data from the buffer
    buffer->removeDataAtBeginning(fcb->common->getLastAckReceived(oppositeFlowDirection));

    fcb->common->lock.release();
}

bool TCPRetransmitter::dataToRetransmit()
{
    unsigned flowDirection = determineFlowDirection();
    auto fcb = _in->fcb_data();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    fcb->common->lock.acquire();

    ByteStreamMaintainer &maintainer = fcb->common->maintainers[oppositeFlowDirection];

    if(!maintainer.isLastAckSentSet() || !fcb->common->lastAckReceivedSet())
    {
        fcb->common->lock.release();
        return false;
    }

    // Check if the buffer is empty
    CircularBuffer *buffer = fcb->common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
    {
        fcb->common->lock.release();
        return false;
    }

    if(buffer->getSize() == 0 || buffer->isBlank())
    {
        fcb->common->lock.release();
        return false;
    }

    uint32_t startOffset = buffer->getStartOffset();
    uint32_t lastAckSent = maintainer.getLastAckSent();

    lastAckSent = fcb->common->maintainers[flowDirection].mapSeq(lastAckSent);

    fcb->common->lock.release();
    // If the value of the last ack sent is greater than the sequence number
    // of the first byte in the buffer, it means that we have sent ACK by ourselves
    // and thus we have data in the buffer waiting to be ACKed
    if(SEQ_LT(startOffset, lastAckSent))
        return true;
    else
        return false;
}

void TCPRetransmitter::retransmissionTimerFired()
{
    // This method is called regularly by a timer to retransmit packets
    // that have been acked by the middlebox but not yet by the receiver
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    auto fcb = _in->fcb_data();

    fcb->common->lock.acquire();

    // Sender segment size
    uint16_t mss = fcb->common->maintainers[flowDirection].getMSS();
    // Set the new slow start threshold
    uint64_t ssthresh = fcb->common->maintainers[flowDirection].getCongestionWindowSize() / 2;
    if(ssthresh < 2 * mss)
        ssthresh = 2 * mss;
    fcb->common->maintainers[flowDirection].setSsthresh(ssthresh);
    // Reset window size
    fcb->common->maintainers[flowDirection].setCongestionWindowSize(mss);

    fcb->common->retransmissionTimings[flowDirection].setLastManualTransmission(
        fcb->common->getLastAckReceived(oppositeFlowDirection));

    // Check if there are data to retransmit and do so if needed
    if(manualTransmission(true))
    {
        // Double the RTO timer as we lost data
        fcb->common->retransmissionTimings[flowDirection].stopTimer();
        fcb->common->retransmissionTimings[flowDirection].startTimerDoubleRTO();
    }

    fcb->common->lock.release();
}

void TCPRetransmitter::transmitMoreData()
{
    unsigned int flowDirection = determineFlowDirection();
    auto fcb = _in->fcb_data();

    if(!fcb->common->lock.attempt())
    {
        // If it is not possible to acquire the lock now, reschedule the timer instead of
        // looping to acquire it
        fcb->common->retransmissionTimings[flowDirection].sendMoreData();
        return;
    }

    // Try to send more data waiting in the buffer

    if(manualTransmission(false))
    {
        // Start the retransmission timer if not already done to be sure that
        // these data will be transmitted correctly
        if(!fcb->common->retransmissionTimings[flowDirection].isTimerRunning())
            fcb->common->retransmissionTimings[flowDirection].startTimer();
    }

    fcb->common->lock.release();
}

bool TCPRetransmitter::manualTransmission(bool retransmission)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    auto fcb = _in->fcb_data();
    fcb->common->lock.acquire();

    // Check if the connection is closed before continuing
    if(fcb->common->state > TCPState::OPEN)
    {
        fcb->common->lock.release();
        return false;
    }

    // Check if we already have tried to send data manually ACKed
    if(!fcb->common->retransmissionTimings[flowDirection].isManualTransmissionDone())
    {
        fcb->common->lock.release();
        return false;
    }

    // Check if there are data in the buffer
    if(!dataToRetransmit())
    {
        fcb->common->lock.release();
        return false;
    }

    ByteStreamMaintainer &maintainer = fcb->common->maintainers[flowDirection];
    ByteStreamMaintainer &otherMaintainer = fcb->common->maintainers[oppositeFlowDirection];
    if(!maintainer.isLastAckSentSet()
        || !otherMaintainer.isLastAckSentSet()
        || !fcb->common->lastAckReceivedSet())
    {
        fcb->common->lock.release();
        return false;
    }

    uint32_t start = 0;

    // Depending on whether this is a retransmission of previously sent data
    // or if we send new data put in the buffer waiting to be transmitted, we get the data
    // from a different point in the buffer
    if(retransmission)
        start = fcb->common->getLastAckReceived(oppositeFlowDirection);
    else
    {
        start = fcb->common->retransmissionTimings[flowDirection].getLastManualTransmission();
        if(SEQ_LT(start, fcb->common->getLastAckReceived(oppositeFlowDirection)))
            start = fcb->common->getLastAckReceived(oppositeFlowDirection);
    }

    uint32_t end = otherMaintainer.getLastAckSent();
    end = maintainer.mapSeq(end);

    if(end <= start)
    {
        fcb->common->lock.release();
        return false;
    }

    uint32_t sizeOfRetransmission = end - start;

    // Check the maximum amount of data we can transmit according to the receiver's window
    // and the congestion window
    sizeOfRetransmission = getMaxAmountData(sizeOfRetransmission, true);

    // Check if we can fit all these data in a packet
    // The fact that the packet might by too big for the receiver or the network will be
    // handled by TCPFragmenter, but we must still ensure that the crafted packet is valid
    // regarding its size
    bool maxReached = false;
    if(sizeOfRetransmission > MAX_TRANSMIT)
    {
        sizeOfRetransmission = MAX_TRANSMIT;
        maxReached = true;
    }

    // If nothing to transmit, abort
    if(sizeOfRetransmission == 0)
    {
        fcb->common->lock.release();
        return false;
    }

    // Otherwise, get the data from the buffer
    fcb->common->retransmissionTimings[flowDirection].getCircularBuffer()->getData(start,
        sizeOfRetransmission, *getBuffer);

    // Forge the packet with the right information
    uint32_t ack = maintainer.getLastAckSent();
    uint32_t ipSrc = maintainer.getIpSrc();
    uint32_t ipDst = maintainer.getIpDst();
    uint16_t portSrc = maintainer.getPortSrc();
    uint16_t portDst = maintainer.getPortDst();
    uint16_t winSize = maintainer.getWindowSize();

    WritablePacket* packet = forgePacket(ipSrc, ipDst, portSrc, portDst, start, ack, winSize,
        TH_ACK, sizeOfRetransmission);

    // Indicate the ACK we are waiting before transmitting more data from the buffer
    uint32_t ackToReceive = start + sizeOfRetransmission;
    fcb->common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);

    setPacketTotalLength(packet, packet->length() - getIPHeaderOffset(packet));

    // Copy the content of the buffer to the payload area
    setPayload(packet, &((*getBuffer)[0]), sizeOfRetransmission);

    // Compute the checksums
    computeTCPChecksum(packet);
    computeIPChecksum(packet);

    // Signal the retransmission to avoid taking retransmitted packets
    // into account to compute the RTT
    if(retransmission)
    {
        fcb->common->retransmissionTimings[flowDirection].signalRetransmission(
            start + sizeOfRetransmission);
    }

    fcb->common->lock.release();

    // Push the packet
    #if HAVE_BATCH
        PacketBatch *batch = PacketBatch::make_from_packet(packet);
        output_push_batch(0, batch);
    #else
        output(0).push(packet);
    #endif

    // Check if we wanted to send more data that we can fit in a packet
    if(maxReached)
    {
        // In this case, we will transmit the rest right now
        manualTransmission(retransmission);
    }

    return true;
}

uint32_t TCPRetransmitter::getMaxAmountData(uint32_t expected, bool canCut)
{
    auto fcb = _in->fcb_data();
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->common->lock.acquire();

    ByteStreamMaintainer &otherMaintainer = fcb->common->maintainers[oppositeFlowDirection];

    // Compute the amount of data "in flight"
    uint32_t inFlight = 0;
    bool manualTransmissionDone =
        fcb->common->retransmissionTimings[flowDirection].isManualTransmissionDone();
    if(manualTransmissionDone)
    {
        // Check if the data we manually transmitted before are ACKed
        uint32_t lastManualTransmission =
            fcb->common->retransmissionTimings[flowDirection].getLastManualTransmission();
        if(SEQ_GT(fcb->common->getLastAckReceived(oppositeFlowDirection)    , lastManualTransmission))
            inFlight = 0;
        else
            inFlight = lastManualTransmission - fcb->common->getLastAckReceived(oppositeFlowDirection);
    }

    // Check that we do not exceed the congestion window's size
    uint64_t cwnd = fcb->common->maintainers[flowDirection].getCongestionWindowSize();
    if(inFlight + expected > cwnd)
    {
        if(canCut)
            expected = cwnd - inFlight;
        else
        {
            fcb->common->lock.release();
            return 0;
        }

    }

    // Check that we do not exceed the receiver's window size
    uint64_t windowSize = otherMaintainer.getWindowSize();
    if(otherMaintainer.getUseWindowScale())
        windowSize *= otherMaintainer.getWindowScale();

    if(inFlight + expected > windowSize)
    {
        if(canCut)
            expected = windowSize - inFlight;
        else
            expected = 0;
    }

    fcb->common->lock.release();

    return expected;
}

void TCPRetransmitter::signalAck(uint32_t ack)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    auto fcb = _in->fcb_data();
    if(!fcb->common || fcb->common->state > TCPState::OPEN)
    {
        fcb->common->lock.release();
        return;
    }


    fcb->common->lock.acquire();

    if(fcb->common->retransmissionTimings[flowDirection].getCircularBuffer() == NULL)
    {
        fcb->common->lock.release();
        return;
    }

    // Prune the buffer to remove received packets
    prune();

    // If all the data have been transmitted, stop the timer
    // Otherwise, restart it
    if(dataToRetransmit())
    {
        fcb->common->lock.release();
        fcb->common->retransmissionTimings[flowDirection].restartTimer();
    }
    else
    {
        fcb->common->lock.release();
        fcb->common->retransmissionTimings[flowDirection].stopTimer();
    }

    // Try to send more data
    fcb->common->retransmissionTimings[flowDirection].sendMoreData();
}

EXPORT_ELEMENT(TCPRetransmitter)
ELEMENT_REQUIRES(false)
ELEMENT_MT_SAFE(TCPRetransmitter)
