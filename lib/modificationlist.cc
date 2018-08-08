#include <click/config.h>
#include <click/glue.hh>
#include <click/modificationlist.hh>
#ifdef CLICK_USERLEVEL
#include <math.h>
#endif
#include <click/memorypool.hh>

CLICK_DECLS

/*
 * modificationlist.cc - Class used to store the modifications performed in a
 * packet's structure
 *
 * Romain Gaillard.
 */

ModificationList::ModificationList(MemoryPool<struct ModificationNode> *poolNodes)
{
    this->poolNodes = poolNodes;
    head = NULL;
    committed = false;
}

ModificationList::~ModificationList()
{
    clear();
}

void ModificationList::clear()
{
    bool freed = false;

    struct ModificationNode* node = head;

    // Browse the list in order to free remaining nodes
    while(node != NULL)
    {
        freed = true; // Indicate that we had to free a node
        struct ModificationNode* next = node->next;
        poolNodes->releaseMemory(node);
        node = next;
    }

    if(freed)
    {
        click_chatter("Warning: modifications in a packet were not committed "
            "before destroying the list");
    }
}

void ModificationList::printList()
{
    struct ModificationNode* node = head;

    // Browse the linked list and display the information of each node
    click_chatter("--- Modification list ---");
    while(node != NULL)
    {
        click_chatter("(%u: %d)", node->position, node->offset);

        node = node->next;
    }
}

bool ModificationList::addModification(uint32_t firstPosition, uint32_t position, int offset)
{
    // The structure refuses new modifications if a commit has been made before
    if(committed)
        return false;

    struct ModificationNode* prev = NULL;
    struct ModificationNode* node = head;

    // Determine where to add the modification in the list
    while(node != NULL && SEQ_LEQ(node->position, position))
    {
        // We translate the requested position which is relative to the current
        // content of the packet to a position relative to the initial content
        // of the packet
        if(SEQ_LT(node->position, position))
        {
            /* Apply the offset of the current node on the requested position
             * A positive offset means that we added data in the new content
             * A negative offset means that we removed data in the new content
             * Thus, we substract the offset
             */
            uint32_t newPosition = 0;

            newPosition = position - node->offset;

            if(SEQ_LT(newPosition, firstPosition))
                newPosition = firstPosition;

            // Ensure that we do not go beyond the position of the current node
            if(SEQ_LT(newPosition, node->position))
                newPosition = node->position;

            // Update the position
            position = newPosition;
        }

        prev = node;
        node = node->next;
    }

    // We went one node too far during the exploration so get back the previous node
    node = prev;

    // Add the node in the linked list
    // Case 1: Reuse an existing node (same positions)
    if(node != NULL && node->position == position)
    {
        // In this case, we simply add the effect of the new modification
        // to the existing node
        node->offset += offset;
    }
    else
    {
        // Case 2: Create a new one
        struct ModificationNode* newNode = poolNodes->getMemory();
        newNode->position = position;
        newNode->offset = offset;

        if(node == NULL)
        {
            // Case 2a: Add the node at the beginning of the list
            newNode->next = head;
            head = newNode;
        }
        else
        {
            // Case 2b: Add the node in the middle or at the end of the list
            newNode->next = node->next;
            node->next = newNode;
        }
    }

    // Try to merge node
    /* Example where merge is required:
     * We have "abcdefgh"
     * We remove "ef" and add to the list: (4, -2)
     * We thus have "abcdgh"
     * We now remove "bcdg", and thus we add to the list (1, -4)
     * The list is thus ((1, -4), (4, -2))
     * We need to merge those two entries into (1, -6)
     */
    mergeNodes();

    return true;
}

void ModificationList::mergeNodes()
{
    struct ModificationNode *prev = NULL;
    struct ModificationNode *node = head;

    // Browse the list of nodes
    while(node != NULL)
    {
        bool merged = false;

        if(prev != NULL)
        {
            // If the previous node exists, we will determine if the data
            // of the current node can be merged with the previous one

            // Determine the range of the modification of the previous node
            uint32_t range = prev->position + (int)abs(prev->offset);

            // If the modification of this node is within the range of the
            // previous one and if they represent both a deletion, merge them
            if(SEQ_LT(node->position, range)
                && prev->offset < 0 && sameSign(node->offset, prev->offset))
            {
                // Remove the current node and merge its value with the previous node
                prev->offset += node->offset;
                prev->next = node->next;
                poolNodes->releaseMemory(node);

                merged = true;
            }
        }

        // Determine the next element
        if(merged)
            node = prev->next;
        else
        {
            prev = node;
            node = node->next;
        }
    }
}

bool ModificationList::isCommitted()
{
    return committed;
}


void ModificationList::commit(ByteStreamMaintainer &maintainer)
{
    struct ModificationNode* node = head;

    // Get the last value in the tree to obtain the effects of
    // the modifications in the previous packets
    // Offsets in the ack tree have the opposite sign with respect to the elements in
    // the modification list
    int offsetTotal = -(maintainer.lastOffsetInAckTree());

    while(node != NULL)
    {
        // Accumulate on the position the effects of the previous modifications
        uint32_t newPositionAck = node->position + offsetTotal;
        offsetTotal += node->offset;

        // The position of the SEQ mapping remains unchanged
        uint32_t newPositionSeq = node->position;

        // Accumulate on the offsets the effects of the previous modifications
        int newOffsetAck = offsetTotal;
        int newOffsetSeq = offsetTotal;

        // The modification to apply to perform the mapping for the ACK has the
        // opposite sign as the modification that was performed so that they
        // counterbalance each other
        newOffsetAck = -(newOffsetAck);

//        click_chatter("Commit %lu %lu %lu %lu",newPositionAck, newPositionSeq, newOffsetAck, newOffsetSeq);

        // Insert the node in the tree. In case of duplicates, keep only the
        // new value
        maintainer.insertInAckTree(newPositionAck, newOffsetAck);
        maintainer.insertInSeqTree(newPositionSeq, newOffsetSeq);

        struct ModificationNode* next = node->next;

        // Remove the node from the list and release memory
        poolNodes->releaseMemory(node);
        node = next;

        // We process the list element by element so the new head is the next node
        head = node;
    }
}

inline bool ModificationList::sameSign(int x, int y)
{
    return ((x <= 0) == (y <= 0));
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_PROVIDES(ModificationList)
