/*
 * bytestreammaintainer.cc - Class used to manage a flow. Stores the modifications in ths flow
 * (bytes removed or inserted) as well as information such as the MSS, ports, ips, last
 * ack received, last ack sent, ...
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <click/rbt.hh>
#include <click/bytestreammaintainer.hh>
#include <click/memorypool.hh>

CLICK_DECLS

ByteStreamMaintainer::ByteStreamMaintainer()
{
    rbtManager = NULL;
    lastAckSent = 0;
    lastPayloadLength = 0;
    lastAckReceived = 0;
    lastSeqSent = 0;
    pruneCounter = 0;
    initialized = false;
    treeAck = NULL;
    treeSeq = NULL;
    windowSize = 32120;
    windowScale = 1;
    useWindowScale = false;
    lastAckSentSet = false;
    lastSeqSentSet = false;
    lastAckReceivedSet = false;
    mss = 536; // Minimum IPV4 MSS
    congestionWindow = mss;
    ssthresh = 65535; // RFC 2001
    dupAcks = 0;
}

void ByteStreamMaintainer::initialize(RBTManager *rbtManager, uint32_t flowStart)
{
    if(initialized)
    {
        click_chatter("Warning: ByteStreamMaintainer already initialized!");
        return;
    }

    this->rbtManager = rbtManager;
    treeAck = RBTreeCreate(rbtManager); // Create the RBT for ACK
    treeSeq = RBTreeCreate(rbtManager); // Create the RBT for Seq

    initialized = true;

    // Insert a key indicating the beginning of the flow. It will be used
    // as a guard
    insertInAckTree(flowStart, 0);
    insertInSeqTree(flowStart, 0);
}

uint32_t ByteStreamMaintainer::mapAck(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        assert(false);
        return 0;
    }

    uint32_t positionSeek = position;

    // Find the element with the greatest key that is less or equal to the given position
    rb_red_blk_node* node = RBFindElementGreatestBelow(treeAck, &positionSeek);
    rb_red_blk_node* pred = treeAck->nil;
    uint32_t nodeKey = 0;
    int nodeInfo = 0;
    uint32_t newPosition = position;

    // If no node found, no mapping to perform
    if(node == treeAck->nil)
        return position;

    // Compute the new position if a node has been found
    nodeKey = *((uint32_t*)node->key);
    nodeInfo = *((int*)node->info);

    newPosition += nodeInfo;

    // Search the predecessor of the node
    pred = TreePredecessor(treeAck, node);

    int predOffset = 0;
    // If the node has a predecessor
    if(pred != treeAck->nil)
    {
        uint32_t predKey = *((uint32_t*)pred->key);
        predOffset = *((int*)pred->info);
    }

    // We check that the value we computed is not below the greatest value we could have
    // obtained via the predecessor
    uint32_t predBound = nodeKey + predOffset;
    if(SEQ_LT(newPosition, predBound))
        newPosition = predBound;

    return newPosition;
}

uint32_t ByteStreamMaintainer::mapSeq(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        assert(false);
        return 0;
    }

    // We do not search the requested position but the position just before it
    // as we do not want to take into account the modifications done at the position itself
    // but the modifications before it for the mapping of the sequence number.
    // For instance, if we send a packet with a sequence number equal to 1, containing
    // a b c d e
    // and then we add "y y y" at the beginning of the next packet (with sequence number equal
    // to 6) containing
    // f g h i j
    // we thus send the packet "y y y f g h i j" and we add to the seq tree the modification
    // 6: 3
    // If we receive a retransmission for the second packet because it was lost, we will map
    // its sequence number (6) and thus have a mapped sequence number equal to 9 (6 + 3).
    // We will therefore not retransmit the added data and the destination will see a gap
    // (it will receive a packet with a sequence number equal to 9 instead of 6).
    // This does not occur if we search the position just before the given one (therefore 5 instead
    // of 6), we will not take into account the modifications in the packet itself.
    uint32_t positionSeek = position - 1;

    // Find the element with the greatest key that is less or equal to the given position
    rb_red_blk_node* node = RBFindElementGreatestBelow(treeSeq, &positionSeek);
    rb_red_blk_node* pred = treeSeq->nil;
    uint32_t newPosition = position;

    // If no node found, no mapping to perform
    if(node == treeSeq->nil)
        return position;

    // Compute the new position
    uint32_t nodeKey = *((uint32_t*)node->key);
    int nodeInfo = *((int*)node->info);

    newPosition += nodeInfo;

    // Search the predecessor of the node
    pred = TreePredecessor(treeSeq, node);

    int predOffset = 0;
    // If the node has a predecessor
    if(pred != treeSeq->nil)
    {
        uint32_t predKey = *((uint32_t*)pred->key);
        predOffset = *((int*)pred->info);
    }

    // We check that the value we computed is not below the greatest value we could have
    // obtained via the predecessor
    uint32_t predBound = nodeKey + predOffset;
    if(SEQ_LT(newPosition, predBound))
        newPosition = predBound;

    return newPosition;
}

void ByteStreamMaintainer::printTrees()
{
    click_chatter("Ack tree:");
    RBTreePrint(treeAck);

    click_chatter("Seq tree:");
    RBTreePrint(treeSeq);
}

void ByteStreamMaintainer::prune(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        assert(false);
        return;
    }

    pruneCounter++;

    // We only prune the trees every time we receive BS_PRUNE_THRESHOLD ACKs
    if(pruneCounter < BS_PRUNE_THRESHOLD)
        return;

    pruneCounter = 0;

    // Remove every element in the tree with a key < position
    RBPrune(treeAck, &position);

    // Map the value to have a valid seq number
    uint32_t positionSeq = mapAck(position);
    // Remove every element in the tree with a key < position
    RBPrune(treeSeq, &positionSeq);

}

void ByteStreamMaintainer::insertInAckTree(uint32_t position, int offset)
{
    insertInTree(treeAck, position, offset);
}

void ByteStreamMaintainer::insertInSeqTree(uint32_t position, int offset)
{
    insertInTree(treeSeq, position, offset);
}

void ByteStreamMaintainer::insertInTree(rb_red_blk_tree* tree, uint32_t position, int offset)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        assert(false);
        return;
    }

    rb_red_blk_node* currentNode = RBExactQuery(tree, &position);
    if(currentNode == tree->nil || currentNode == NULL)
    {
        // Node did not already exist, insert
        uint32_t *newKey = ((RBTManager*)tree->manager)->allocateKey();
        *newKey = position;
        int *newInfo = ((RBTManager*)tree->manager)->allocateInfo();
        *newInfo = offset;

        RBTreeInsert(tree, newKey, newInfo);
    }
    else
    {
        // Node already exists, replace the old value
        *((int*)currentNode->info) = offset;
    }
}

int ByteStreamMaintainer::lastOffsetInAckTree()
{
    // Return the offset with the greatest key in the ack tree or 0 if the tree is empty
    rb_red_blk_node* current = RBMax(treeAck);

    if(current == treeAck->nil)
        return 0;
    else
        return *((int*)current->info);
}

ByteStreamMaintainer::~ByteStreamMaintainer()
{
    if(initialized)
    {
        if (treeAck)
            RBTreeDestroy(treeAck);
        treeAck = 0;
        if (treeSeq)
            RBTreeDestroy(treeSeq);
        treeSeq = 0;
        initialized = false;
    }
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(RBT)
ELEMENT_REQUIRES(ModificationList)
ELEMENT_PROVIDES(ByteStreamMaintainer)
