#ifndef MIDDLEBOX_FCB_HH
#define MIDDLEBOX_FCB_HH

#include <clicknet/tcp.h>
#include <click/hashtable.hh>
#include <click/bytestreammaintainer.hh>
#include <click/modificationlist.hh>
#include <click/memorypool.hh>
#include "tcpclosingstate.hh"
#include "tcpreordernode.hh"

/**
 * This file is used to simulate the FCB provided by Middleclick
 */

struct fcb_tcp_common
{
    // One maintainer for each direction of the connection
    ByteStreamMaintainer maintainers[2];
};

struct fcb_tcpreorder
{
    struct TCPPacketListNode* packetList;
    tcp_seq_t expectedPacketSeq;
    MemoryPool<struct TCPPacketListNode> *pool;

    fcb_tcpreorder()
    {
        packetList = NULL;
        expectedPacketSeq = 0;
        pool = NULL;
    }

    ~fcb_tcpreorder()
    {
        // Clean the list and free memory
        struct TCPPacketListNode* node = packetList;
        struct TCPPacketListNode* toDelete = NULL;

        while(node != NULL)
        {
            toDelete = node;
            node = node->next;

            // Kill packet
            toDelete->packet->kill();

            // Put back node in memory pool
            pool->releaseMemory(toDelete);
        }
        packetList = NULL;
    }
};

struct fcb_tcpin
{
    HashTable<tcp_seq_t, ModificationList*> modificationLists;
    TCPClosingState::Value closingState;
    MemoryPool<struct ModificationList>* poolModificationLists;
    MemoryPool<struct ModificationNode>* poolModificationNodes;

    fcb_tcpin() : modificationLists(NULL)
    {
        closingState = TCPClosingState::OPEN;
        poolModificationLists = NULL;
        poolModificationNodes = NULL;
    }

    ~fcb_tcpin()
    {
        // Put back in the corresponding memory pool all the modification lists
        // in use (in the hashtable)
        for(HashTable<tcp_seq_t, ModificationList*>::iterator it = modificationLists.begin();
            it != modificationLists.end(); ++it)
        {
            // Call the destructor to release object's own memory
            (it.value())->~ModificationList();
            // Put it back in the pool
            poolModificationLists->releaseMemory(it.value());
        }
    }
};

struct fcb_httpin
{
    bool headerFound;

    fcb_httpin()
    {
        headerFound = false;
    }
};

struct fcb_pathmerger
{
    HashTable<tcp_seq_t, int> portMap;

    fcb_pathmerger() : portMap(-1)
    {
    }
};

struct fcb
{
    struct fcb_tcp_common tcp_common;

    struct fcb_tcpreorder tcpreorder;
    struct fcb_tcpin tcpin;
    struct fcb_httpin httpin;
    struct fcb_pathmerger pathmerger;
};

// Global array of the two FCBs corresponding to each direction of the
// connection
extern struct fcb fcbArray[2];

#endif
