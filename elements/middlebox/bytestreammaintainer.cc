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
    windowSize = 32120;
    lastAckSentSet = false;
    lastSeqSentSet = false;
    lastAckReceivedSet = false;
}

void ByteStreamMaintainer::initialize(RBTMemoryPoolStreamManager *rbtManager, uint32_t flowStart)
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

    // Insert a key indicating the beginning of the flow that will be used
    // as a guard
    insertInAckTree(flowStart, 0);
    insertInSeqTree(flowStart, 0);
}

uint32_t ByteStreamMaintainer::mapAck(uint32_t position)
{
    if(!initialized)
    {
        click_chatter("Error: ByteStreamMaintainer is not initialized");
        return 0;
    }

    uint32_t positionSeek = position;

    rb_red_blk_node* node = RBFindElementGreatestBelow(treeAck, &positionSeek);
    rb_red_blk_node* pred = treeAck->nil;
    rb_red_blk_node* succ = treeAck->nil;
    uint32_t nodeKey = 0;
    int nodeInfo = 0;
    uint32_t newPosition = position;

    if(node == treeAck->nil)
        return position;

    // Compute the new position
    nodeKey = *((uint32_t*)node->key);
    nodeInfo = *((int*)node->info);

    newPosition += nodeInfo;

    pred = TreePredecessor(treeAck, node);
    succ = TreeSuccessor(treeAck, node);

    int predOffset = 0;
    // If the node has a predecessor
    if(pred != treeAck->nil)
    {
        uint32_t predKey = *((uint32_t*)pred->key);
        predOffset = *((int*)pred->info);
    }

    uint32_t predBound = nodeKey + predOffset;

    if(SEQ_LT(newPosition, predBound))
        newPosition = predBound;

    // Check that the computed value is at most equal to the lowest value
    // obtained via the successor
    if(succ != treeAck->nil)
    {
        uint32_t succPosition = *((uint32_t*)succ->key);
        int succOffset = *((int*)succ->info);

        if(succOffset > 0)
        {
            uint32_t succBound = 0;

            succBound = succPosition + succOffset;

            if(SEQ_GT(newPosition, succBound))
                newPosition = succBound;
        }
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

    uint32_t positionSeek = position - 1;

    rb_red_blk_node* node = RBFindElementGreatestBelow(treeSeq, &positionSeek);
    rb_red_blk_node* pred = treeSeq->nil;
    uint32_t newPosition = position;

    if(node == treeSeq->nil)
        return position;

    // Compute the new position
    uint32_t nodeKey = *((uint32_t*)node->key);
    int nodeInfo = *((int*)node->info);

    newPosition += nodeInfo;

    pred = TreePredecessor(treeSeq, node);

    int predOffset = 0;
    // If the node has a predecessor
    if(pred != treeSeq->nil)
    {
        uint32_t predKey = *((uint32_t*)pred->key);
        predOffset = *((int*)pred->info);
    }

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
    // TODO comment
    if(!lastAckSentSet || SEQ_GT(ackNumber, lastAckSent))
        lastAckSent = ackNumber;

    lastAckSentSet = true;
}

uint32_t ByteStreamMaintainer::getLastAckSent()
{
    if(!lastAckSentSet)
        click_chatter("Error: Last ack sent not defined");

    return lastAckSent;
}

void ByteStreamMaintainer::setLastSeqSent(uint32_t seqNumber)
{
    // TODO comment
    if(!lastSeqSentSet || SEQ_GT(seqNumber, lastSeqSent))
        lastSeqSent = seqNumber;

    lastSeqSentSet = true;
}

uint32_t ByteStreamMaintainer::getLastSeqSent()
{
    if(!lastSeqSentSet)
        click_chatter("Error: Last sequence sent not defined");

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
    // TODO comment
    if(!lastAckReceivedSet || SEQ_GT(ackNumber, lastAckReceived))
        lastAckReceived = ackNumber;

    lastAckReceivedSet = true;
}

uint32_t ByteStreamMaintainer::getLastAckReceived()
{
    if(!lastAckReceivedSet)
        click_chatter("Error: Last ack received not defined");

    return lastAckReceived;
}

bool ByteStreamMaintainer::isLastAckSentSet()
{
    return lastAckSentSet;
}

bool ByteStreamMaintainer::isLastAckReceivedSet()
{
    return lastAckReceivedSet;
}

bool ByteStreamMaintainer::isLastSeqSentSet()
{
    return lastSeqSentSet;
}

void ByteStreamMaintainer::setWindowSize(uint16_t windowSize)
{
    this->windowSize = windowSize;
}

uint16_t ByteStreamMaintainer::getWindowSize()
{
    return windowSize;
}

void ByteStreamMaintainer::setIpSrc(uint32_t ipSrc)
{
    this->ipSrc = ipSrc;
}

uint32_t ByteStreamMaintainer::getIpSrc()
{
    return ipSrc;
}

void ByteStreamMaintainer::setIpDst(uint32_t ipDst)
{
    this->ipDst = ipDst;
}

uint32_t ByteStreamMaintainer::getIpDst()
{
    return ipDst;
}

void ByteStreamMaintainer::setPortSrc(uint16_t portSrc)
{
    this->portSrc = portSrc;
}

uint16_t ByteStreamMaintainer::getPortSrc()
{
    return portSrc;
}

void ByteStreamMaintainer::setPortDst(uint16_t portDst)
{
    this->portDst = portDst;
}

uint16_t ByteStreamMaintainer::getPortDst()
{
    return portDst;
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
