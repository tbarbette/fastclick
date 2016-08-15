#ifndef MIDDLEBOX_MODIFICATIONLIST_HH
#define MIDDLEBOX_MODIFICATIONLIST_HH

/*
 * modificationlist.hh - Class used to store the modifications performed in a
 * packet's structure
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include "memorypool.hh"
#include "bytestreammaintainer.hh"

CLICK_DECLS

class ByteStreamMaintainer;

/**
 * @brief Structure representing a node in the linked list of modifications
 */
struct ModificationNode
{
        uint32_t position; /** Position of the modification */
        int offset; /** Offset of the modification (positive if bytes are added
                        or negative if bytes are removed) */
        struct ModificationNode* next; /** Pointer to the next node */
};

/**
 * @class ModificationList
 * @brief Class used to store the modifications performed in a packet's structure
 *
 * This class is used to store the modifications made in the structure of a
 * packet. It stores the positions at which bytes are inserted and removed
 * so that they can be committed when the packet is in its final state.
 * The modifications are committed to a ByteStreamMaintainer.
 */
class ModificationList
{
    // TCPOut is the only component allowed to commit modifications
    // It is thus a friend which can access the private method "commit"
    friend class TCPOut;

public:
    /** @brief Construct a ModificationList
     * @param poolNodes A pointer to a MemoryPool for the nodes of type
     * struct ModificationNode.
     */
    ModificationList(MemoryPool<struct ModificationNode>* poolNodes);

    /** @brief Destruct a ModificationList and free the memory
     */
    ~ModificationList();

    /** @brief Print the current state of the list in the console using
     * click_chatter
     */
    void printList();

    /** @brief Add a modification in the list
     * @param firstPosition The lowest position possible for this packet
     * @param position The position at which the modification occurs
     * @param offset The offset representing the modification (a positive
     * number if bytes are added and a negative number if bytes are removed)
     * @return True if the modification has been taken into account and false
     * if the list does not accept any new modification because a commit
     * has been made.
     */
    bool addModification(uint32_t firstPosition, uint32_t position, int offset);

    /** @brief Indicate whether the list has been committed
     * @return True if the list has been committed
     */
    bool isCommitted();

    /** @brief Commit the modifications stored in the list into a ByteStreamMaintainer
     * @param maintainer The ByteStreamMaintainer in which the modifications will be committed
     */
    void commit(ByteStreamMaintainer& maintainer);

private:
    /** @brief Indicate whether two integers have the same sign
     * @param first The first integer
     * @param second The second integer
     * @return  True if both integers have the same sign
     */
    bool sameSign(int first, int second);

    /** @brief Merge nodes that represent overlapping deletions
     */
    void mergeNodes();

    /** @brief Clear the modification list
     */
    void clear();

    MemoryPool<struct ModificationNode> *poolNodes;
    struct ModificationNode *head;
    bool committed; /** Becomes true when a commit is made */
};


CLICK_ENDDECLS

#endif
