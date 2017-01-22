#include <click/bytestreammaintainer.hh>
#include <stdlib.h>
#include <click/config.h>
#include <click/glue.hh>
#include <click/rbt.hh>
#include <click/memorypool.hh>

ByteStreamMaintainer::ByteStreamMaintainer()
{
    rbtManager = new RBTMemoryPoolManager();
    treeAck = RBTreeCreate(rbtManager); // Create the RBT for ACK
    treeSeq = RBTreeCreate(rbtManager); // Create the RBT for Seq
    lastAck = 0;
    pruneCounter = 0;
}

unsigned int ByteStreamMaintainer::mapAck(unsigned int position)
{
    rb_red_blk_node* node = RBFindElementGreatestBelow(treeAck, &position);
    rb_red_blk_node* succ = treeAck->nil;
    unsigned int newPosition = position;

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
        unsigned int succPosition = *((unsigned int*)succ->key);
        int succOffset = *((int*)succ->info);

        unsigned int succBound = 0;

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

unsigned int ByteStreamMaintainer::mapSeq(unsigned int position)
{
    rb_red_blk_node* node = RBFindElementGreatestBelow(treeSeq, &position);
    rb_red_blk_node* pred = treeSeq->nil;
    unsigned int newPosition = position;

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
    unsigned int predPosition = 0;
    int predOffset = 0;

    if(pred != treeSeq->nil)
    {
        predPosition = *((unsigned int*)pred->key);
        predOffset = *((int*)pred->info);
    }

    unsigned int predBound = 0;

    if(predOffset < 0 && -(predOffset) > predPosition)
        predBound = predPosition;
    else
        predBound = predPosition + predOffset;

    if(predBound < predPosition)
        predBound = predPosition;

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

void ByteStreamMaintainer::prune(unsigned int position)
{
    // Remove every element in the tree with a key < position
    RBPrune(treeAck, &position);

    // Map the given position to the seq tree and prune it
    unsigned int positionSeq = mapAck(position);
    RBPrune(treeSeq, &positionSeq);
}

void ByteStreamMaintainer::ackReceived(unsigned int ackNumber)
{
    pruneCounter++;

    if(ackNumber > lastAck)
        lastAck = ackNumber;

    // Avoid pruning at every ack
    if(pruneCounter >= BS_PRUNE_THRESHOLD)
    {
        pruneCounter = 0;
        prune(lastAck);
    }
}

void ByteStreamMaintainer::insertInAckTree(unsigned int position, int offset)
{
    insertInTree(treeAck, position, offset);
}

void ByteStreamMaintainer::insertInSeqTree(unsigned int position, int offset)
{
    insertInTree(treeSeq, position, offset);
}

void ByteStreamMaintainer::insertInTree(rb_red_blk_tree* tree, unsigned int position, int offset)
{
    rb_red_blk_node* currentNode = RBExactQuery(tree, &position);
    if(currentNode == tree->nil || currentNode == NULL)
    {
        // Node did not already exist, insert
        unsigned int *newKey = ((RBTMemoryPoolManager*)tree->manager)->allocateKey();
        *newKey = position;
        int *newInfo = ((RBTMemoryPoolManager*)tree->manager)->allocateInfo();
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
    RBTreeDestroy(treeAck);
    RBTreeDestroy(treeSeq);
    delete rbtManager;
}
