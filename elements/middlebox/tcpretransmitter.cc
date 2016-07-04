#include <click/config.h>
#include <click/glue.hh>
#include <click/timestamp.hh>
#include <clicknet/tcp.h>
#include "rbt.hh"
#include "memorypool.hh"
#include "bytestreammaintainer.hh"
#include "tcpretransmitter.hh"
#include "tcpretransmissionnode.hh"
#include "tcpelement.hh"

TCPRetransmitter::TCPRetransmitter()
{

}

TCPRetransmitter::~TCPRetransmitter()
{

}


int TCPRetransmitter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    return 0;
}

void TCPRetransmitter::push(int port, Packet *packet)
{
    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    struct fcb* fcb = &fcbArray[flowDirection];
    if(fcb->tcpretransmitter.tree == NULL)
    {
        // Create the tree for this flow if not already done
        fcb->tcpretransmitter.tree = RBTreeCreate(&rbtManager);
    }

    rb_red_blk_tree* tree = fcb->tcpretransmitter.tree;

    unsigned oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    if(port == 0)
    {
        // The packet comes on the normal input
        // We thus need to add it in the tree and let it continue
        Packet* toInsert = packet->clone();

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
}

void TCPRetransmitter::prune(struct fcb *fcb)
{
    rb_red_blk_tree* tree = fcb->tcpretransmitter.tree;
    unsigned flowDirection = determineFlowDirection();
    unsigned oppositeFlowDirection = 1 - flowDirection;
    ByteStreamMaintainer &maintainer = fcb->tcp_common->maintainers[oppositeFlowDirection];

    // TODO prune the tree
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
ELEMENT_REQUIRES(RBT)
EXPORT_ELEMENT(TCPRetransmitter)
