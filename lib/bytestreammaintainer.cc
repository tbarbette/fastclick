#include <click/bytestreammaintainer.hh>
#include <stdlib.h>
#include <click/config.h>
#include <click/glue.hh>
#include <click/rbt.hh>
#include <click/memorypool.hh>

ByteStreamMaintainer::ByteStreamMaintainer()
{
    rbtManager = new RBTMemoryPoolManager();
    tree = RBTreeCreate(rbtManager);
    lastAck = 0;
    pruneCounter = 0;
    seqOffset = 0;
}

void ByteStreamMaintainer::newInsertion(unsigned int startingPosition, int length)
{
    if(length == 0)
        return;

    // Update the global offset
    seqOffset += length;
    unsigned int seqNextPacket = startingPosition + length; // The updated offset of the next packet
    // Add a node at key "seqNextPacket"

    rb_red_blk_node* currentNode = RBExactQuery(tree, &seqNextPacket);
    if(currentNode == tree->nil || currentNode == NULL)
    {
        // Node did not exist, insert
        int previousOffset = getAckOffset(seqNextPacket);

        unsigned int *newKey = ((RBTMemoryPoolManager*)tree->manager)->allocateKey();
        *newKey = seqNextPacket;
        int *newInfo = ((RBTMemoryPoolManager*)tree->manager)->allocateInfo();
        *newInfo = previousOffset - length;

        RBTreeInsert(tree, newKey, newInfo);
    }
    else
    {
        // Node already exists, update
        *((int*)currentNode->info) = *((int*)currentNode->info) - length;
    }
}

void ByteStreamMaintainer::newDeletion(unsigned int startingPosition, int length)
{
    newInsertion(startingPosition, -length);
}

int ByteStreamMaintainer::getAckOffset(unsigned int position)
{
    // Get the largest key with value <= position and return its value.
    // If the key contains "removed", give the successor
    int *offset = (int*)RBFindElementGreatestAbove(tree, &position);

    if(offset == NULL)
        return 0;

    return *offset;
}

void ByteStreamMaintainer::printTree()
{
    RBTreePrint(tree);
}

int ByteStreamMaintainer::getSeqOffset()
{
    return seqOffset;
}

void ByteStreamMaintainer::prune(unsigned int position)
{
    // Compute the sum of the values of the keys <= position
    // Add (or update) the key at the given position with the computed value
    // Removed keys <= position

    int total = 0;
    total = RBPrune(tree, tree->root->left, &position);

    // We add a new key
    unsigned int *newKey = ((RBTMemoryPoolManager*)tree->manager)->allocateKey();
    *newKey = position;
    int *newInfo = ((RBTMemoryPoolManager*)tree->manager)->allocateInfo();
    *newInfo = total;

    RBTreeInsert(tree, newKey, newInfo);
}

void ByteStreamMaintainer::ackReceived(unsigned int ackNumber)
{
    pruneCounter++;

    if(ackNumber > lastAck)
        lastAck = ackNumber;

    if(pruneCounter >= BS_PRUNE_THRESHOLD)
    {
        pruneCounter = 0;
        prune(lastAck);
    }
}

ByteStreamMaintainer::~ByteStreamMaintainer()
{
    RBTreeDestroy(tree);
    delete rbtManager;
}
