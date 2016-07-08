#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include "rbt.hh"
#include "bytestreammaintainer.hh"
#include "memorypool.hh"

CLICK_DECLS

ByteStreamMaintainer::ByteStreamMaintainer()
{
    rbtManager = NULL;
    lastAckSent = 0;
    lastAckReceived = 0;
    lastSeqSent = 0;
    pruneCounter = 0;
    initialized = false;
    treeAck = NULL;
    treeSeq = NULL;
}

void ByteStreamMaintainer::initialize(RBTMemoryPoolStreamManager *rbtManager)
{
    this->rbtManager = rbtManager;
    treeAck = RBTreeCreate(rbtManager); // Create the RBT for ACK
    treeSeq = RBTreeCreate(rbtManager); // Create the RBT for Seq
    initialized = true;
}

uint32_t ByteStreamMaintainer::mapAck(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        return 0;
    }

    rb_red_blk_node* node = RBFindElementGreatestBelow(treeAck, &position);
    rb_red_blk_node* succ = treeAck->nil;
    uint32_t newPosition = position;

    if(node != treeAck->nil)
    {
        // Compute the new position
        int nodeInfo = *((int*)node->info);

        if(nodeInfo < 0 && -(nodeInfo) > newPosition)
            newPosition = 0;
        else
            newPosition += nodeInfo;

        succ = TreeSuccessor(treeAck, node);
    }

    // Check that the computed value is at most equal to the lowest value
    // obtained via the successor
    if(succ != treeAck->nil)
    {
        uint32_t succPosition = *((uint32_t*)succ->key);
        int succOffset = *((int*)succ->info);

        uint32_t succBound = 0;

        if(succOffset < 0 && -(succOffset) > succPosition)
            succBound = 0;
        else
            succBound = succPosition + succOffset;

        if(succBound < succPosition)
            succBound = succPosition;

        if(newPosition > succBound)
            newPosition = succBound;
    }

    return newPosition;
}

uint32_t ByteStreamMaintainer::mapSeq(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        return 0;
    }

    rb_red_blk_node* node = RBFindElementGreatestBelow(treeSeq, &position);
    rb_red_blk_node* pred = treeSeq->nil;
    uint32_t newPosition = position;

    if(node != treeSeq->nil)
    {
        // Compute the new position
        int nodeInfo = *((int*)node->info);

        if(nodeInfo < 0 && -(nodeInfo) > newPosition)
            newPosition = 0;
        else
            newPosition += nodeInfo;

        pred = TreePredecessor(treeSeq, node);
    }

    if(newPosition < 0)
        newPosition = 0;

    // Check that the computed value is at least equal to the lowest value
    // obtained via the predecessor
    uint32_t predPosition = 0;
    int predOffset = 0;

    if(pred != treeSeq->nil)
    {
        predPosition = *((uint32_t*)pred->key);
        predOffset = *((int*)pred->info);
    }

    uint32_t predBound = 0;

    if(predOffset < 0 && -(predOffset) > predPosition)
        predBound = predPosition;
    else
        predBound = predPosition + predOffset;

    if(newPosition < predBound)
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
        return;
    }

    pruneCounter++;

    // Avoid pruning at every ack
    if(pruneCounter < BS_PRUNE_THRESHOLD)
        return;

    click_chatter("Tree pruned");
    pruneCounter = 0;

    // Remove every element in the tree with a key < position
    RBPrune(treeAck, &position);

    // Map the value to have a valid seq number
    uint32_t positionSeq = mapAck(position);
    RBPrune(treeSeq, &positionSeq);

}

void ByteStreamMaintainer::setLastAckSent(uint32_t ackNumber)
{
    if(ackNumber > lastAckSent)
        lastAckSent = ackNumber;
}

uint32_t ByteStreamMaintainer::getLastAckSent()
{
    return lastAckSent;
}

void ByteStreamMaintainer::setLastSeqSent(uint32_t seqNumber)
{
    if(seqNumber > lastSeqSent)
        lastSeqSent = seqNumber;
}

uint32_t ByteStreamMaintainer::getLastSeqSent()
{
    return lastSeqSent;
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
        return;
    }

    rb_red_blk_node* currentNode = RBExactQuery(tree, &position);
    if(currentNode == tree->nil || currentNode == NULL)
    {
        // Node did not already exist, insert
        uint32_t *newKey = ((RBTMemoryPoolStreamManager*)tree->manager)->allocateKey();
        *newKey = position;
        int *newInfo = ((RBTMemoryPoolStreamManager*)tree->manager)->allocateInfo();
        *newInfo = offset;

        RBTreeInsert(tree, newKey, newInfo);
    }
    else
    {
        // Node already exists, replace the old value
        *((int*)currentNode->info) = offset;
    }
}

void ByteStreamMaintainer::setLastAckReceived(uint32_t ackNumber)
{
    if(ackNumber > lastAckReceived)
        lastAckReceived = ackNumber;
}

uint32_t ByteStreamMaintainer::getLastAckReceived()
{
    return lastAckReceived;
}

int ByteStreamMaintainer::lastOffsetInAckTree()
{
    // Return the offset with the greatest key in the ack tree
    // or 0 if the tree is empty
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
        RBTreeDestroy(treeAck);
        RBTreeDestroy(treeSeq);
    }
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(RBT)
ELEMENT_REQUIRES(ModificationList)
ELEMENT_PROVIDES(ByteStreamMaintainer)
