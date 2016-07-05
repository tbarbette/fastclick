#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/timestamp.hh>
#include <clicknet/tcp.h>
#include "bufferpool.hh"
#include "bufferpoolnode.hh"
#include "bytestreammaintainer.hh"
#include "tcpretransmitter.hh"
#include "tcpelement.hh"

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

    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    struct fcb* fcb = &fcbArray[flowDirection];

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

    // TODO this part must be reworked, this is old code that used trees and packets
    // It must be adapted to the circular buffers
    /*
    rb_red_blk_tree* tree = fcb->tcpretransmitter.tree;

    unsigned oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    if(port == 0)
    {
        // The packet comes on the normal input
        // We thus need to add it in the tree and let it continue
        click_chatter("Packet cloned: %p", packet);
        Packet* toInsert = packet->clone();
        click_chatter("Insert before: %p", toInsert);
        click_chatter("Begnning shared? %d / %d", packet->shared(), toInsert->shared());
        toInsert = toInsert->uniqueify();
        click_chatter("End shared? %d / %d", packet->shared(), toInsert->shared());

        uint32_t seq = TCPElement::getSequenceNumber(toInsert);
        // Check if the packet was not already in the tree
        rb_red_blk_node* currentNode = RBExactQuery(tree, &seq);
        if(currentNode == tree->nil || currentNode == NULL)
        {
            uint32_t *newKey = ((RBTMemoryPoolRetransmissionManager*)tree->manager)->allocateKey();
            *newKey = seq;
            struct TCPRetransmissionNode *newInfo = ((RBTMemoryPoolRetransmissionManager*)tree->manager)->allocateInfo();

            // Set the pointer to the packet
            newInfo->packet = toInsert;
            // Set the last transmission of the packet to now
            // (because packets are inserted after their first transmission)
            newInfo->lastTransmission.assign_now();

            click_chatter("Packet %u added to retransmission tree", seq);

            // Insert the packet in the tree
            RBTreeInsert(tree, newKey, newInfo);
        }

        // Push the packet to the next element
        output(0).push(packet);

        // Prune the tree to remove received packets
        prune(fcb);
    }
    else
    {
        // The packet comes on the second input
        // It means we have a retransmission.
        // We need to perform the mapping between the received packet and
        // the data to retransmit from the tree

        // The given packet is "raw", meaning the sequence number and the ack
        // are unmodified. Thus, we need to perform the right mappings to
        // have the connection with the packets in the tree

        // Get the sequence number that will be the key of the packet in the tree
        uint32_t seq = TCPElement::getSequenceNumber(packet);

        uint32_t mappedSeq = maintainer.mapSeq(seq);

        uint32_t payloadSize = TCPElement::getPayloadLength(packet);
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

        // TODO Get the data to retransmit from the tree

        // We drop the retransmitted packet
        packet->kill();

        // TODO We push the corresponding data from the tree
    }
    */
}

void TCPRetransmitter::prune(struct fcb *fcb)
{
    unsigned flowDirection = determineFlowDirection();
    unsigned oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    // TODO prune the buffer
}

void TCPRetransmitter::retransmitSelfAcked(struct fcb *fcb)
{
    // This method is called regularly by a timer to retransmit packets
    // that have been acked by the middlebox but not yet by the receiver

    // TODO
    // The timer is linked to each flow (thus it is in the fcb) and must
    // have a reference to the fcb so it can give it to this method
    // The interval of the timer will depend on the estimated initial RTT
}


ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
EXPORT_ELEMENT(TCPRetransmitter)
