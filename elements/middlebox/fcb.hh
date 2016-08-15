#ifndef MIDDLEBOX_FCB_HH
#define MIDDLEBOX_FCB_HH

#include <clicknet/tcp.h>
#include <click/hashtable.hh>
#include <click/ipflowid.hh>
#include <click/timer.hh>
#include <click/sync.hh>
#include <click/multithread.hh>
#include "bytestreammaintainer.hh"
#include "modificationlist.hh"
#include "memorypool.hh"
#include "rbt.hh"
#include "tcpclosingstate.hh"
#include "tcpreordernode.hh"
#include "circularbuffer.hh"
#include "retransmissiontiming.hh"
#include "flowbuffer.hh"

/**
 * This file is used to simulate the FCB provided by Middleclick
 */

/**
 * Common structure accessed by both sides of a TCP connection.
 * The lock must be acquired before accessing the members of the structure
 */
struct fcb_tcp_common
{
    // One maintainer for each direction of the connection
    ByteStreamMaintainer maintainers[2];
    // One retransmission manager for each direction of the connection
    RetransmissionTiming retransmissionTimings[2];
    // State of the connection
    TCPClosingState::Value closingStates[2];
    // Lock to ensure that only one side of the flow (one thread) at a time
    // accesses the common structure
    Spinlock lock;

    fcb_tcp_common()
    {
        closingStates[0] = TCPClosingState::OPEN;
        closingStates[1] = TCPClosingState::OPEN;
    }

    ~fcb_tcp_common()
    {
    }
};

/**
 * Structure used by the TCPReorder element
 */
struct fcb_tcpreorder
{
    struct TCPPacketListNode* packetList;
    tcp_seq_t expectedPacketSeq;
    tcp_seq_t lastSent;
    MemoryPool<struct TCPPacketListNode> *pool;

    fcb_tcpreorder()
    {
        packetList = NULL;
        expectedPacketSeq = 0;
        lastSent = 0;
        pool = NULL;
    }

    ~fcb_tcpreorder()
    {
        // Clean the list and free memory
        struct TCPPacketListNode* node = packetList;
        struct TCPPacketListNode* toDelete = NULL;

        if(pool != NULL)
        {
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
    }
};

/**
 * Structure used by the TCPIn element
 */
struct fcb_tcpin
{
    HashTable<tcp_seq_t, ModificationList*> modificationLists;
    MemoryPool<struct ModificationList>* poolModificationLists;
    MemoryPool<struct ModificationNode>* poolModificationNodes;

    // Members used to be able to free memory for the tcp_common structure when destructed
    MemoryPool<struct fcb_tcp_common>* poolTcpCommon;
    HashTable<IPFlowID, struct fcb_tcp_common*> *tableTcpCommon;
    Spinlock *lock; // Lock for the 2 structures above
    bool inChargeOfTcpCommon;
    struct fcb_tcp_common *commonToDelete;
    IPFlowID flowID;

    fcb_tcpin()
    {
        poolModificationLists = NULL;
        poolModificationNodes = NULL;
        lock = NULL;

        poolTcpCommon = NULL;
        commonToDelete = NULL;
        inChargeOfTcpCommon = false;
        tableTcpCommon = NULL;
    }

    ~fcb_tcpin()
    {
        // Put back in the corresponding memory pool all the modification lists
        // in use (in the hashtable)
        for(HashTable<tcp_seq_t, ModificationList*>::iterator it = modificationLists.begin();
            it != modificationLists.end(); ++it)
        {
            // Call the destructor to release the object's own memory
            (it.value())->~ModificationList();
            // Put it back in the pool
            poolModificationLists->releaseMemory(it.value());
        }

        // Because tcp_common has been allocated manually by TCPIn
        // we need, to free its memory manually if we are the creator
        if(inChargeOfTcpCommon && commonToDelete != NULL)
        {
            commonToDelete->~fcb_tcp_common();
            lock->acquire();
            tableTcpCommon->erase(flowID);
            poolTcpCommon->releaseMemory(commonToDelete);
            lock->release();
            commonToDelete = NULL;
        }
    }
};

/**
 * Structure used by the HTTPIn element
 */
struct fcb_httpin
{
    bool headerFound;
    char url[2048];
    char method[16];
    uint64_t contentLength;
    uint64_t contentSeen;
    bool isRequest;

    fcb_httpin()
    {
        headerFound = false;
        contentSeen = 0;
        contentLength = 0;
        url[0] = '\0';
        method[0] = '\0';
        isRequest = false;
    }
};

/**
 * Structure used by the PathMerger element
 */
struct fcb_pathmerger
{
    HashTable<tcp_seq_t, int> portMap;

    fcb_pathmerger() : portMap(0)
    {
    }
};

/**
 * Structure used by the TCPMarkMSS element
 */
struct fcb_tcpmarkmss
{
    uint16_t mss;

    fcb_tcpmarkmss()
    {
        mss = 0;
    }
};

/**
 * Structure used by the HTTPOut element
 */
struct fcb_httpout
{
    FlowBuffer flowBuffer;
};

/**
 * Structure used by the InsultRemover element
 */
struct fcb_insultremover
{
    FlowBuffer flowBuffer;
    uint32_t counterRemoved;

    fcb_insultremover()
    {
        counterRemoved = 0;
    }
};

/**
 * Global structure used by the elements to store information about a flow
 */
struct fcb
{
    struct fcb_tcp_common* tcp_common;

    struct fcb_tcpreorder tcpreorder;
    struct fcb_tcpin tcpin;
    struct fcb_httpin httpin;
    struct fcb_httpout httpout;
    struct fcb_pathmerger pathmerger;
    struct fcb_insultremover insultremover;
    struct fcb_tcpmarkmss tcpmarkmss;

    fcb()
    {
        tcp_common = NULL;
    }

    ~fcb()
    {

    }
};

// Global array of the two FCBs corresponding to each direction of the
// connection

// IMPORTANT note about the global variables used to simulate the FCB system:
// As the FCB is represented by a global variable for the purpose of the simulation, when the
// program is closed, it is destructed after all the others elements. Thus, the destructor
// of the structures in the FCB may try to access variables that do not exist anymore as the
// elements such as TCPOut and TCPIn have already been destroyed (as they are destructed before
// global variables), for instance memory pools.
// This may lead to an invalid memory access when the program is closed.
//
// This problem will not occur when the system is integrated to Middleclick as the FCB will
// be destructed before the elements such as TCPOut and TCPIn and will be able to access the
// right variables. Therefore, this is just a side effect of the simulation and does not have
// any consequence.
extern struct fcb fcbArray[2];

#endif
