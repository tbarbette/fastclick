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

TCPRetransmitter::TCPRetransmitter() : circularPool(TCPRETRANSMITTER_BUFFER_NUMBER), getBuffer(TCPRETRANSMITTER_GET_BUFFER_SIZE, '\0')
{
    rawBufferPool = NULL;
}

TCPRetransmitter::~TCPRetransmitter()
{
    if(rawBufferPool != NULL)
        delete rawBufferPool;
}

int TCPRetransmitter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    uint32_t initialBufferSize = 65535;

    if(Args(conf, this, errh)
    .read_p("INITIALBUFFERSIZE", initialBufferSize)
    .complete() < 0)
        return -1;

    rawBufferPool = new BufferPool(TCPRETRANSMITTER_BUFFER_NUMBER, initialBufferSize);

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
        EXECUTE_FOR_EACH_PACKET_DROPPABLE_FCB(processPacketNormal, &fcbArray[flowDirection], batch, [](Packet* p){})
    else
        EXECUTE_FOR_EACH_PACKET_DROPPABLE_FCB(processPacketRetransmission, &fcbArray[flowDirection], batch, [](Packet* p){})

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

    RetransmissionTiming &manager = fcb->tcp_common->retransmissionTimings[flowDirection];

    // If timer has not been initialized yet, do it
    if(!manager.isTimerInitialized())
        manager.initTimer(fcb, this);

    // If this had not been done previously, assign a circular buffer
    // for this flow
    if(manager.getCircularBuffer() == NULL)
    {
        CircularBuffer *circularBuffer = circularPool.getMemory();
        // Call the constructor of CircularBuffer with the pool of raw buffers
        circularBuffer = new(circularBuffer) CircularBuffer(rawBufferPool);
        manager.setCircularBuffer(circularBuffer, &circularPool);
    }
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
        uint32_t ackToReceiveMapped = fcb->tcp_common->maintainers[flowDirection].mapAck(seq + contentSize);

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
                return NULL;
            }

            fcb->tcp_common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);
        }

        // Start a new RTT measure if possible
        fcb->tcp_common->retransmissionTimings[flowDirection].startRTTMeasure(seq);
    }

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
        click_chatter("The source is trying to retransmit ACKed data, resending an ACK.");

        // Resend the ACK for this packet
        requestMorePackets(fcb, packet);
        packet->kill();
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
    if(sizeOfRetransmission <= 0)
    {
        click_chatter("Nothing to retransmit for packet with sequence %u", seq);
        packet->kill();
        return NULL;
    }

    click_chatter("Retransmitting %u bytes", sizeOfRetransmission);

    // Get the data from the buffer
    manager.getCircularBuffer()->getData(mappedSeq, sizeOfRetransmission, getBuffer);
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
    setPayload(newPacket, &getBuffer[0], sizeOfRetransmission);

    // Recompute the checksums
    computeTCPChecksum(newPacket);
    computeIPChecksum(newPacket);

    // Signal the retransmission to avoind taking retransmitted packets
    // into account to compute the RTT
    fcb->tcp_common->retransmissionTimings[flowDirection].signalRetransmission(mappedSeq + payloadSize);

    return newPacket;
}

void TCPRetransmitter::prune(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    CircularBuffer *buffer = fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
        return;

    if(!maintainer.isLastAckReceivedSet())
        return;

    // We remove ACKed data in the buffer
    buffer->removeDataAtBeginning(maintainer.getLastAckReceived());

    click_chatter("Retransmission buffer pruned! (new size: %u, ack: %u)", buffer->getSize(), maintainer.getLastAckReceived());
}

bool TCPRetransmitter::dataToRetransmit(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    if(!maintainer.isLastAckSentSet() || !maintainer.isLastAckReceivedSet())
        return false;

    CircularBuffer *buffer = fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer();
    if(buffer == NULL)
        return false;

    if(buffer->getSize() == 0 || buffer->isBlank())
        return false;

    uint32_t startOffset = buffer->getStartOffset();
    uint32_t lastAckSent = maintainer.getLastAckSent();

    lastAckSent = fcb->tcp_common->maintainers[flowDirection].mapSeq(lastAckSent);

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

    click_chatter("Timer fired");

    if(manualTransmission(fcb, true))
    {
        fcb->tcp_common->retransmissionTimings[flowDirection].stopTimer();
        fcb->tcp_common->retransmissionTimings[flowDirection].startTimerDoubleRTO();
    }
}

void TCPRetransmitter::transmitMoreData(struct fcb* fcb)
{
    unsigned int flowDirection = determineFlowDirection();

    if(manualTransmission(fcb, false))
    {
        if(!fcb->tcp_common->retransmissionTimings[flowDirection].isTimerRunning())
            fcb->tcp_common->retransmissionTimings[flowDirection].startTimer();
    }
}

bool TCPRetransmitter::manualTransmission(struct fcb *fcb, bool retransmission)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->tcp_common->closingStates[flowDirection] != TCPClosingState::OPEN)
        return false;

    if(!fcb->tcp_common->retransmissionTimings[flowDirection].isManualTransmissionDone())
        return false;

    if(!dataToRetransmit(fcb))
        return false;

    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];
    ByteStreamMaintainer &otherMaintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];
    if(!maintainer.isLastAckSentSet() || !otherMaintainer.isLastAckSentSet() || !otherMaintainer.isLastAckReceivedSet())
        return false;

    uint32_t start = 0;

    if(retransmission)
        start = otherMaintainer.getLastAckReceived();
    else
        start = fcb->tcp_common->retransmissionTimings[flowDirection].getLastManualTransmission();

    uint32_t end = otherMaintainer.getLastAckSent();
    end = maintainer.mapSeq(end);

    if(end <= start)
        return false;

    click_chatter("There are data to transmit");

    uint32_t sizeOfRetransmission = end - start;

    sizeOfRetransmission = getMaxAmountData(fcb, sizeOfRetransmission, true);

    if(sizeOfRetransmission == 0)
        return false;

    fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer()->getData(start, sizeOfRetransmission, getBuffer);

    uint32_t ack = maintainer.getLastAckSent();
    uint32_t ipSrc = maintainer.getIpSrc();
    uint32_t ipDst = maintainer.getIpDst();
    uint16_t portSrc = maintainer.getPortSrc();
    uint16_t portDst = maintainer.getPortDst();
    uint16_t winSize = maintainer.getWindowSize();

    WritablePacket* packet = forgePacket(ipSrc, ipDst, portSrc, portDst, start, ack, winSize, TH_ACK, sizeOfRetransmission);

    uint32_t ackToReceive = start + sizeOfRetransmission;
    fcb->tcp_common->retransmissionTimings[flowDirection].setLastManualTransmission(ackToReceive);

    setPacketTotalLength(packet, packet->length() - getIPHeaderOffset(packet));

    // Copy the content of the buffer to the payload area
    setPayload(packet, &getBuffer[0], sizeOfRetransmission);

    click_chatter("Transmitting manually %u bytes.", sizeOfRetransmission);
    // Compute the checksums
    computeTCPChecksum(packet);
    computeIPChecksum(packet);

    // Signal the retransmission to avoind taking retransmitted packets
    // into account to compute the RTT
    if(retransmission)
        fcb->tcp_common->retransmissionTimings[flowDirection].signalRetransmission(start + sizeOfRetransmission);

    // Push the packet
    #if HAVE_BATCH
        PacketBatch *batch = PacketBatch::make_from_packet(packet);
        output_push_batch(0, batch);
    #else
        output(0).push(packet);
    #endif

    return true;
}

uint16_t TCPRetransmitter::getMaxAmountData(struct fcb *fcb, uint16_t expected, bool canCut)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    ByteStreamMaintainer &otherMaintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    // Check that we do not exceed the receiver's window size
    uint64_t windowSize = otherMaintainer.getWindowSize();
    if(otherMaintainer.getUseWindowScale())
        windowSize *= otherMaintainer.getWindowScale();
    if(expected > windowSize)
    {
        if(canCut)
        {
            expected = windowSize;
            otherMaintainer.setWindowSize(0);
            click_chatter("Manual retransmission limited to %lu bytes by receiver's window size", expected);
        }
        else
        {
            click_chatter("Manual retransmission aborted (%u) by receiver's window size: %lu", expected, windowSize);
            expected = 0;
        }
    }
    else
    {
        windowSize -= expected;
        if(otherMaintainer.getUseWindowScale())
            windowSize /= otherMaintainer.getWindowScale();
        otherMaintainer.setWindowSize(windowSize);
    }

    return expected;
}

void TCPRetransmitter::signalAck(struct fcb* fcb, uint32_t ack)
{
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;

    if(fcb->tcp_common->retransmissionTimings[flowDirection].getCircularBuffer() == NULL)
        return;

    if(fcb->tcp_common->closingStates[flowDirection] != TCPClosingState::OPEN)
        return;

    // Prune the buffer to remove received packets
    prune(fcb);

    // If all the data have been transmitted, stop the timer
    // Otherwise, restart it
    if(dataToRetransmit(fcb))
        fcb->tcp_common->retransmissionTimings[flowDirection].restartTimer();
    else
        fcb->tcp_common->retransmissionTimings[flowDirection].stopTimer();

    // Try to send more data
    fcb->tcp_common->retransmissionTimings[flowDirection].sendMoreData();
}

ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_REQUIRES(RetransmissionTiming)
EXPORT_ELEMENT(TCPRetransmitter)
