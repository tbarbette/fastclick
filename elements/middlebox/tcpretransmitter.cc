/*
 * tcpretransmitter.{cc,hh} -- manages the tcp retransmissions and ensures that data we are
 * responsible for (data we ACKed) is correctly received and that their transmission
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
#include "bufferpool.hh"
#include "bufferpoolnode.hh"
#include "bytestreammaintainer.hh"
#include "tcpretransmitter.hh"
#include "tcpelement.hh"
#include "retransmissiontiming.hh"

TCPRetransmitter::TCPRetransmitter()
{
    // Initialize the memory pools and buffer of each thread
    for(unsigned int i = 0; i < circularPool.size(); ++i)
        circularPool.get_value(i).initialize(TCPRETRANSMITTER_BUFFER_NUMBER);

    for(unsigned int i = 0; i < getBuffer.size(); ++i)
        getBuffer.get_value(i).resize(TCPRETRANSMITTER_GET_BUFFER_SIZE, '\0');

    for(unsigned int i = 0; i < rawBufferPool.size(); ++i)
        rawBufferPool.get_value(i) = NULL;
}

TCPRetransmitter::~TCPRetransmitter()
{
    for(unsigned int i = 0; i < rawBufferPool.size(); ++i)
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

    for(unsigned int i = 0; i < rawBufferPool.size(); ++i)
        rawBufferPool.get_value(i) = new BufferPool(TCPRETRANSMITTER_BUFFER_NUMBER, initialBufferSize);

    return 0;
}

void TCPRetransmitter::push_packet(int port, Packet *packet)
{

    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    struct fcb* fcb = &fcbArray[flowDirection];

    // Packets on the first input follow the normal path (first time transmitted)
    // packet on the second input are retransmitted packets
    if(port == 0)
        packet = processPacketNormal(fcb, packet);
    else
        packet = processPacketRetransmission(fcb, packet);

    if(packet != NULL)
        output(0).push(packet);
}

#if HAVE_BATCH
void TCPRetransmitter::push_batch(int port, PacketBatch *batch)
{
    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    struct fcb* fcb = &fcbArray[flowDirection];

    if(port == 0)
        EXECUTE_FOR_EACH_PACKET_DROPPABLE_FCB(processPacketNormal, fcb, batch, [](Packet* p){})
    else
        EXECUTE_FOR_EACH_PACKET_DROPPABLE_FCB(processPacketRetransmission,fcb, batch, [](Packet* p){})

    if(batch != NULL)
        output_push_batch(0, batch);
}
#endif

void TCPRetransmitter::checkInitialization(struct fcb *fcb)
{
    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();

    fcb->tcp_common->lock.acquire();

    RetransmissionTiming &manager = fcb->tcp_common->retransmissionTimings[flowDirection];

    // If timer has not been initialized yet, do it
    if(!manager.isTimerInitialized())
        manager.initTimer(fcb, this);

    // If this had not been done previously, assign a circular buffer
    // for this flow
    if(manager.getCircularBuffer() == NULL)
    {
        CircularBuffer *circularBuffer = (*circularPool).getMemory();
        // Call the constructor of CircularBuffer with the pool of raw buffers
        circularBuffer = new(circularBuffer) CircularBuffer(*rawBufferPool);
        manager.setCircularBuffer(circularBuffer, &(*circularPool));
    }

    fcb->tcp_common->lock.release();
}

Packet* TCPRetransmitter::processPacketNormal(struct fcb *fcb, Packet *packet)
{
    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->tcp_common == NULL)
    {
        packet->kill();
        return NULL;
    }

    fcb->tcp_common->lock.acquire();

    RetransmissionTiming &manager = fcb->tcp_common->retransmissionTimings[flowDirection];
    checkInitialization(fcb);
    CircularBuffer *buffer = manager.getCircularBuffer();
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];

    // The packet comes on the normal input
    // We thus need to add its content in the buffer and let it continue
    const unsigned char* content = getPayloadConst(packet);
    uint32_t contentSize = getPayloadLength(packet);
    uint32_t seq = getSequenceNumber(packet);

    if(contentSize > 0)
    {
        // If this is the first content added to the buffer, we indicate
        // the buffer the sequence number as an offset
        // so that we will be able to give sequence number as indexes in
        // the future
        if(buffer->isBlank())
            buffer->setStartOffset(seq);

        // Add packet content to the buffer
        buffer->addDataAtEnd(content, contentSize);

        uint32_t lastAckSent = 0;
        bool lastAckSentSet = fcb->tcp_common->maintainers[oppositeFlowDirection].isLastAckSentSet();
        if(lastAckSentSet)
            lastAckSent = fcb->tcp_common->maintainers[oppositeFlowDirection].getLastAckSent();

        // ackToReceive is the ACK that will be received for the current
        // packet. We map it to be able to compare it with lastAckSent
        // which is mapped
        uint32_t ackToReceive = seq + contentSize;
        if(isFin(packet) || isSyn(packet))
            ackToReceive++;
        uint32_t ackToReceiveMapped = fcb->tcp_common->maintainers[flowDirection].mapAck(ackToReceive);

        if(lastAckSentSet && SEQ_LEQ(ackToReceiveMapped, lastAckSent))
        {
            // We know that we are transmitting ACKed data
            // We thus start the retransmission timer
            fcb->tcp_common->retransmissionTimings[flowDirection].startTimer();

            uint16_t sizeOfTransmission = getMaxAmountData(fcb, getPayloadLength(packet), false);

            // We delay the transmission of this data
            if(sizeOfTransmission == 0)
            {
                packet->kill();
                fcb->tcp_common->lock.release();
                return NULL;
            }

            fcb->tcp_common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);
        }

        // Start a new RTT measure if possible
        fcb->tcp_common->retransmissionTimings[flowDirection].startRTTMeasure(seq);
    }

    fcb->tcp_common->lock.release();
    return packet;
}

Packet* TCPRetransmitter::processPacketRetransmission(struct fcb *fcb, Packet *packet)
{
    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->tcp_common == NULL)
    {
        packet->kill();
        return NULL;
    }

    fcb->tcp_common->lock.acquire();

    RetransmissionTiming &manager = fcb->tcp_common->retransmissionTimings[flowDirection];
    checkInitialization(fcb);
    CircularBuffer *buffer = manager.getCircularBuffer();
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];

    // The packet comes on the second input
    // It means we have a retransmission.
    // We need to perform the mapping between the received packet and
    // the data to retransmit from the tree

    // The given packet is "raw", meaning the sequence number and the ack
    // are unmodified. Thus, we need to perform the right mappings to
    // have the connection with the packets in the tree

    if(fcb->tcp_common->closingStates[flowDirection] != TCPClosingState::OPEN)
    {
        packet->kill();
        fcb->tcp_common->lock.release();
        return NULL;
    }

    // Get the sequence number that will be the key of the packet in the buffer
    uint32_t seq = getSequenceNumber(packet);

    uint32_t lastAckSent = fcb->tcp_common->maintainers[oppositeFlowDirection].getLastAckSent();

    if(SEQ_LT(seq, lastAckSent))
    {
        // We must check the lastAckSent by the other side
        // If we receive data already ACKed, we must drop them and make the other side
        // resend the last ACK, because it means that the ACK was lost.
        // (We send an ACK and we receive retransmission nevertheless)

        // Resend the ACK for this packet
        setInitialAck(packet, getAckNumber(packet));
        requestMorePackets(fcb, packet, true);
        packet->kill();
        fcb->tcp_common->lock.release();
        return NULL;
    }

    uint32_t mappedSeq = maintainer.mapSeq(seq);

    uint32_t payloadSize = getPayloadLength(packet);
    uint32_t mappedSeqEnd = maintainer.mapSeq(seq + payloadSize);

    // The lower bound of the interval to retransmit is "mappedSeq"
    // The higher bound of the interval to retransmit is "mappedSeqEnd"
    // mappedSeqEnd is not just mappedSeq + payloadSize as it must take
    // into account the data removed inside the packet

    // Check if we really have something to retransmit
    // If the full content of the packet was removed, mappedSeqEnd = mappedSeq
    uint32_t sizeOfRetransmission = mappedSeqEnd - mappedSeq;

    // If the packet is just a FIN or RST packet, we let it go as it is, with seq and ack mapped
    if(getPayloadLength(packet) == 0 && (isFin(packet) || isRst(packet)))
    {
        WritablePacket *newPacket = packet->uniqueify();

        // Map the previous ACK
        uint32_t ack = getAckNumber(newPacket);
        ack = fcb->tcp_common->maintainers[oppositeFlowDirection].mapAck(ack);
        setAckNumber(newPacket, ack);
        setSequenceNumber(newPacket, mappedSeq);

        // Recompute the checksums
        computeTCPChecksum(newPacket);
        computeIPChecksum(newPacket);

        fcb->tcp_common->lock.release();
        return newPacket;
    }

    if(sizeOfRetransmission <= 0)
    {
        packet->kill();
        fcb->tcp_common->lock.release();
        return NULL;
    }

    // Get the data from the buffer
    manager.getCircularBuffer()->getData(mappedSeq, sizeOfRetransmission, *getBuffer);
    // The data to retransmit are now in "getBuffer"

    // Map the previous ACK
    uint32_t ack = getAckNumber(packet);
    ack = fcb->tcp_common->maintainers[oppositeFlowDirection].mapAck(ack);

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

    // Signal the retransmission to avoind taking retransmitted packets
    // into account to compute the RTT
    fcb->tcp_common->retransmissionTimings[flowDirection].signalRetransmission(mappedSeq + payloadSize);

    fcb->tcp_common->lock.release();
    return newPacket;
}

void TCPRetransmitter::prune(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->tcp_common->lock.acquire();
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    CircularBuffer *buffer = fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
    {
        fcb->tcp_common->lock.release();
        return;
    }

    if(!maintainer.isLastAckReceivedSet())
    {
        fcb->tcp_common->lock.release();
        return;
    }

    // We remove ACKed data in the buffer
    buffer->removeDataAtBeginning(maintainer.getLastAckReceived());

    fcb->tcp_common->lock.release();
}

bool TCPRetransmitter::dataToRetransmit(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    fcb->tcp_common->lock.acquire();

    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    if(!maintainer.isLastAckSentSet() || !maintainer.isLastAckReceivedSet())
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    // Check if the buffer is empty
    CircularBuffer *buffer = fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    if(buffer->getSize() == 0 || buffer->isBlank())
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    uint32_t startOffset = buffer->getStartOffset();
    uint32_t lastAckSent = maintainer.getLastAckSent();

    lastAckSent = fcb->tcp_common->maintainers[flowDirection].mapSeq(lastAckSent);

    fcb->tcp_common->lock.release();
    // If the value of the last ack sent is greater than the sequence number
    // of the first in the buffer, it means we have sent ACK by ourselves
    // and thus we have data in the buffer waiting to be acked
    if(SEQ_LT(startOffset, lastAckSent))
        return true;
    else
        return false;
}

void TCPRetransmitter::retransmissionTimerFired(struct fcb* fcb)
{
    // This method is called regularly by a timer to retransmit packets
    // that have been acked by the middlebox but not yet by the receiver
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->tcp_common->lock.acquire();

    // Sender segment size
    uint16_t mss = fcb->tcp_common->maintainers[oppositeFlowDirection].getMSS();
    // Set the new slow start threshold
    uint64_t ssthresh = fcb->tcp_common->maintainers[flowDirection].getCongestionWindowSize() / 2;
    if(ssthresh < 2 * mss)
        ssthresh = 2 * mss;
    fcb->tcp_common->maintainers[flowDirection].setSsthresh(ssthresh);
    // Reset window size
    fcb->tcp_common->maintainers[flowDirection].setCongestionWindowSize(mss);

    fcb->tcp_common->retransmissionTimings[flowDirection].setLastManualTransmission(
        fcb->tcp_common->maintainers[oppositeFlowDirection].getLastAckReceived());

    // Check if there are data to retransmit and do so if needed
    if(manualTransmission(fcb, true))
    {
        // Double the RTO timer as we lost data
        fcb->tcp_common->retransmissionTimings[flowDirection].stopTimer();
        fcb->tcp_common->retransmissionTimings[flowDirection].startTimerDoubleRTO();
    }

    fcb->tcp_common->lock.release();
}

void TCPRetransmitter::transmitMoreData(struct fcb* fcb)
{
    unsigned int flowDirection = determineFlowDirection();

    fcb->tcp_common->lock.acquire();
    // Try to send more data waiting in the buffer

    if(manualTransmission(fcb, false))
    {
        // Start the retransmission timer if not already done to be sure that
        // these data will be transmitted correctly
        if(!fcb->tcp_common->retransmissionTimings[flowDirection].isTimerRunning())
            fcb->tcp_common->retransmissionTimings[flowDirection].startTimer();
    }

    fcb->tcp_common->lock.release();
}

bool TCPRetransmitter::manualTransmission(struct fcb *fcb, bool retransmission)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->tcp_common->lock.acquire();

    // Check if the connection is closed before continuing
    if(fcb->tcp_common->closingStates[flowDirection] != TCPClosingState::OPEN)
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    // Check if we already have tried to send data manually ACKed
    if(!fcb->tcp_common->retransmissionTimings[flowDirection].isManualTransmissionDone())
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    // Check if there are data in the buffer
    if(!dataToRetransmit(fcb))
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];
    ByteStreamMaintainer &otherMaintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];
    if(!maintainer.isLastAckSentSet()
        || !otherMaintainer.isLastAckSentSet()
        || !otherMaintainer.isLastAckReceivedSet())
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    uint32_t start = 0;

    // Depending on whether this is a retransmission of previously sent data
    // or if we send new data put in the buffer waiting to be transmitted, we get the data
    // from a different point in the buffer
    if(retransmission)
        start = otherMaintainer.getLastAckReceived();
    else
    {
        start = fcb->tcp_common->retransmissionTimings[flowDirection].getLastManualTransmission();
        if(SEQ_LT(start, otherMaintainer.getLastAckReceived()))
            start = otherMaintainer.getLastAckReceived();
    }

    uint32_t end = otherMaintainer.getLastAckSent();
    end = maintainer.mapSeq(end);

    if(end <= start)
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    uint32_t sizeOfRetransmission = end - start;

    // Check the maximum amount of data we can transmit according to the receiver's window
    // and the congestion window
    sizeOfRetransmission = getMaxAmountData(fcb, sizeOfRetransmission, true);

    // If nothing to transmit, abort
    if(sizeOfRetransmission == 0)
    {
        fcb->tcp_common->lock.release();
        return false;
    }

    // Otherwise, get the data from the buffer
    fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer()->getData(start,
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

    // Indicate the ACK we are waiting to transmit more data from the buffer
    uint32_t ackToReceive = start + sizeOfRetransmission;
    fcb->tcp_common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);

    setPacketTotalLength(packet, packet->length() - getIPHeaderOffset(packet));

    // Copy the content of the buffer to the payload area
    setPayload(packet, &((*getBuffer)[0]), sizeOfRetransmission);

    // Compute the checksums
    computeTCPChecksum(packet);
    computeIPChecksum(packet);

    // Signal the retransmission to avoind taking retransmitted packets
    // into account to compute the RTT
    if(retransmission)
    {
        fcb->tcp_common->retransmissionTimings[flowDirection].signalRetransmission(
            start + sizeOfRetransmission);
    }

    fcb->tcp_common->lock.release();

    // Push the packet
    #if HAVE_BATCH
        PacketBatch *batch = PacketBatch::make_from_packet(packet);
        output_push_batch(0, batch);
    #else
        output(0).push(packet);
    #endif

    return true;
}

uint32_t TCPRetransmitter::getMaxAmountData(struct fcb *fcb, uint32_t expected, bool canCut)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->tcp_common->lock.acquire();

    ByteStreamMaintainer &otherMaintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    // Compute the amount of data "in flight"
    uint32_t inFlight = 0;
    bool manualTransmissionDone =
        fcb->tcp_common->retransmissionTimings[flowDirection].isManualTransmissionDone();
    if(manualTransmissionDone)
    {
        // Check if the data we manually transmitted before are ACKed
        uint32_t lastManualTransmission =
            fcb->tcp_common->retransmissionTimings[flowDirection].getLastManualTransmission();
        if(SEQ_GT(otherMaintainer.getLastAckReceived(), lastManualTransmission))
            inFlight = 0;
        else
            inFlight = lastManualTransmission - otherMaintainer.getLastAckReceived();
    }

    // Check that we do not exceed the congestion window's size
    uint64_t cwnd = fcb->tcp_common->maintainers[flowDirection].getCongestionWindowSize();
    if(inFlight + expected > cwnd)
    {
        if(canCut)
            expected = cwnd - inFlight;
        else
        {
            fcb->tcp_common->lock.release();
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

    fcb->tcp_common->lock.release();

    return expected;
}

void TCPRetransmitter::signalAck(struct fcb* fcb, uint32_t ack)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    fcb->tcp_common->lock.acquire();

    if(fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer() == NULL)
    {
        fcb->tcp_common->lock.release();
        return;
    }

    if(fcb->tcp_common->closingStates[flowDirection] != TCPClosingState::OPEN)
    {
        fcb->tcp_common->lock.release();
        return;
    }

    // Prune the buffer to remove received packets
    prune(fcb);

    // If all the data have been transmitted, stop the timer
    // Otherwise, restart it
    if(dataToRetransmit(fcb))
    {
        fcb->tcp_common->lock.release();
        fcb->tcp_common->retransmissionTimings[flowDirection].restartTimer();
    }
    else
    {
        fcb->tcp_common->lock.release();
        fcb->tcp_common->retransmissionTimings[flowDirection].stopTimer();
    }

    // Try to send more data
    fcb->tcp_common->retransmissionTimings[flowDirection].sendMoreData();
}

ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_REQUIRES(RetransmissionTiming)
EXPORT_ELEMENT(TCPRetransmitter)
ELEMENT_MT_SAFE(TCPRetransmitter)
