#include <click/retransmissionmanager.hh>
#include <click/config.h>
#include <click/glue.hh>
#include <click/timestamp.hh>
#include <clicknet/tcp.h>
#include <click/rbt.hh>
#include <click/memorypool.hh>
#include <click/bytestreammaintainer.hh>

RetransmissionManager::RetransmissionManager()
{
    maintainer = NULL;
    rbtManager = new RBTMemoryPoolRetransmissionManager();
    tree = RBTreeCreate(rbtManager);
}

RetransmissionManager::~RetransmissionManager()
{
    RBTreeDestroy(tree);
    delete rbtManager;
}

bool RetransmissionManager::insertPacket(Packet *packet)
{
    // Clone the given packet to be able to insert it independently in the tree
    Packet* toInsert = packet->clone();

    // Get the sequence number that will be the key of the packet in the tree
    const click_tcp *tcph = toInsert->tcp_header();
    uint32_t seq = ntohl(tcph->th_seq);

    // Check if the packet was not already in the tree
    rb_red_blk_node* currentNode = RBExactQuery(tree, &seq);
    if(currentNode == tree->nil || currentNode == NULL)
    {
        uint32_t *newKey = ((RBTMemoryPoolRetransmissionManager*)tree->manager)->allocateKey();
        *newKey = seq;
        struct RetransmissionNode *newInfo = ((RBTMemoryPoolRetransmissionManager*)tree->manager)->allocateInfo();

        // Set the pointer to the packet
        newInfo->packet = toInsert;
        // Set the last transmission of the packet to now
        // (because packets are inserted after their first transmission)
        newInfo->lastTransmission.assign_now();

        // Insert the packet in the tree
        RBTreeInsert(tree, newKey, newInfo);

        return true;
    }
    else
        return false; // The packet has not been added in the tree
}

void RetransmissionManager::retransmit(Packet *packet)
{
    // The given packet is "raw", meaning the sequence number and the ack
    // are unmodified. Thus, we need to perform the right mappings to
    // have the connection with the packets in the tree
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    // Get the sequence number that will be the key of the packet in the tree
    uint32_t seq = ntohl(tcph->th_seq);
    uint32_t mappedSeq = maintainer.mapSeq(seq);

    uint32_t payloadSize = ip_len - iph_len - tcp_offset;
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

}

void RetransmissionManager::ackReceived(uint32_t ackNumber)
{
    // The ack number given indicates the sequence number of until which
    // packets have been received. This number is as it has been sent
    // by the recipient of the data, it must therefore be mapped to correspond
    // to sequence numbers in the tree
    uint32_t mappedSeq = maintainer.mapSeq(ackNumber);

    // Prune the tree
}
