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

void TCPRetransmitter::push(int port, Packet *packet)
{

    // Simulate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    unsigned int oppositeFlowDirection = 1 - flowDirection;
    struct fcb* fcb = &fcbArray[flowDirection];

    bool dataSent = false;
    RetransmissionTiming &manager = fcb->tcp_common->retransmissionTimings[flowDirection];

    // If timer has not been initialized yet, do it
    if(!manager.isTimerInitialized())
        manager.initTimer(fcb, this, TCPRetransmitter::retransmissionTimerFired);

    if(fcb->tcpretransmitter.buffer == NULL)
    {
        // If this had not been done previously, assign a circular buffer
        // for this flow
        CircularBuffer *circularBuffer = circularPool.getMemory();
        // Call the constructor of CircularBuffer with the pool of raw buffers
        circularBuffer = new(circularBuffer) CircularBuffer(rawBufferPool);
        fcb->tcpretransmitter.buffer = circularBuffer;
        fcb->tcpretransmitter.bufferPool = &circularPool;
    }


    CircularBuffer *buffer = fcb->tcpretransmitter.buffer;

    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];

    if(port == 0)
    {
        // The packet comes on the normal input
        // We thus need to add its content in the buffer and let it continue
        const unsigned char* content = getPayload(packet);
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

            click_chatter("Packet %u added to retransmission buffer (%u bytes)", seq, contentSize);

            dataSent = true;

            // Start a new RTT measure if possible
            fcb->tcp_common->retransmissionTimings[flowDirection].startRTTMeasure(seq);
        }
        // Prune the tree to remove received packets
        prune(fcb);

        // Push the packet to the next element
        output(0).push(packet);
    }
    else
    {
        // We must check the lastAckSent by the other side
        // If we receive data already ACKed, we must drop them and make the other side
        // resend the last ACK, because it means that the ACK was lost.
        // (We send an ACK and we receive retransmission nevertheless)
        // Idea: add "lostAck" function in the stack so that TCPIn will ack
        // the packet for us

        // The packet comes on the second input
        // It means we have a retransmission.
        // We need to perform the mapping between the received packet and
        // the data to retransmit from the tree

        // The given packet is "raw", meaning the sequence number and the ack
        // are unmodified. Thus, we need to perform the right mappings to
        // have the connection with the packets in the tree

        // Get the sequence number that will be the key of the packet in the buffer
        uint32_t seq = getSequenceNumber(packet);

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
            return;
        }

        click_chatter("Retransmitting %u bytes", sizeOfRetransmission);

        // Get the data from the buffer
        fcb->tcpretransmitter.buffer->getData(mappedSeq, sizeOfRetransmission, getBuffer);
        // The data to retransmit is now in "getBuffer"

        // Map the previous ACL
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

        // Set the new size of the packet in the IP header (total size minus ethernet header)
        setPacketTotalLength(newPacket, newPacket->length() - getIPHeaderOffset(packet));

        // Copy the content of the buffer to the payload area
        setPayload(newPacket, &getBuffer[0], sizeOfRetransmission);

        // Recompute the checksums
        computeTCPChecksum(newPacket);
        computeIPChecksum(newPacket);

        fcb->tcp_common->retransmissionTimings[flowDirection].signalRetransmission(mappedSeq + payloadSize);

        // Push the packet with its new content
        output(0).push(newPacket);

        dataSent = true;
    }

    if(dataSent)
    {
        // Start timer here as we just sent data
    }

}

void TCPRetransmitter::prune(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[flowDirection];

    // We remove ACKed data in the buffer
    fcb->tcpretransmitter.buffer->removeDataAtBeginning(maintainer.getLastAckReceived());

    click_chatter("Retransmission buffer pruned! (new size: %u, ack: %u)", fcb->tcpretransmitter.buffer->getSize(), maintainer.getLastAckReceived());
}

void TCPRetransmitter::retransmissionTimerFired(Timer *timer, void *data)
{
    // This method is called regularly by a timer to retransmit packets
    // that have been acked by the middlebox but not yet by the receiver
    struct fcb *fcb = (struct fcb*)data;
}

ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_REQUIRES(RetransmissionTiming)
EXPORT_ELEMENT(TCPRetransmitter)
