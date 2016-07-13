#ifndef MIDDLEBOX_FCB_HH
#define MIDDLEBOX_FCB_HH

#include <clicknet/tcp.h>
#include <click/hashtable.hh>
#include <click/ipflowid.hh>
#include <click/timer.hh>
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

struct fcb_tcp_common
{
    // One maintainer for each direction of the connection
    ByteStreamMaintainer maintainers[2];
    // One retransmission manager for each direction of the connecytion
    RetransmissionTiming retransmissionTimings[2];
    // State of the connection
    TCPClosingState::Value closingStates[2];

    fcb_tcp_common()
    {
        closingStates[0] = TCPClosingState::OPEN;
        closingStates[1] = TCPClosingState::OPEN;
    }

    ~fcb_tcp_common()
    {
    }
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

struct fcb_tcpin
{
    HashTable<tcp_seq_t, ModificationList*> modificationLists;
    MemoryPool<struct ModificationList>* poolModificationLists;
    MemoryPool<struct ModificationNode>* poolModificationNodes;

    // Members used to be able to free memory for tcp_common
    // destroyed
    MemoryPool<struct fcb_tcp_common>* poolTcpCommon;
    bool inChargeOfTcpCommon;
    struct fcb_tcp_common *commonToDelete;
    IPFlowID flowID;
    HashTable<IPFlowID, struct fcb_tcp_common*> *tableTcpCommon;

    fcb_tcpin()
    {
        poolModificationLists = NULL;
        poolModificationNodes = NULL;

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
            // Call the destructor to release object's own memory
            (it.value())->~ModificationList();
            // Put it back in the pool
            poolModificationLists->releaseMemory(it.value());
        }

        // Because tcp_common has been allocated manually by TCPIn
        // we need, to free its memory manually if we are the creator
        if(inChargeOfTcpCommon && commonToDelete != NULL)
        {
            commonToDelete->~fcb_tcp_common();
            tableTcpCommon->erase(flowID);
            poolTcpCommon->releaseMemory(commonToDelete);
            commonToDelete = NULL;
        }
    }
};

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

struct fcb_pathmerger
{
    HashTable<tcp_seq_t, int> portMap;

    fcb_pathmerger() : portMap(0)
    {
    }
};


struct fcb_tcpretransmitter
{
    CircularBuffer *buffer;
    MemoryPool<CircularBuffer> *bufferPool;

    fcb_tcpretransmitter()
    {
        buffer = NULL;
        bufferPool = NULL;
    }

    ~fcb_tcpretransmitter()
    {
        if(buffer != NULL && bufferPool != NULL)
        {
            // Release memory for the circular buffer
            buffer->~CircularBuffer();
            bufferPool->releaseMemory(buffer);
        }
    }
};

struct fcb_tcpmarkmss
{
    uint16_t mss;

    fcb_tcpmarkmss()
    {
        mss = 0;
    }
};

struct fcb_httpout
{
    FlowBuffer flowBuffer;
};

struct fcb
{
    struct fcb_tcp_common* tcp_common;

    struct fcb_tcpreorder tcpreorder;
    struct fcb_tcpin tcpin;
    struct fcb_httpin httpin;
    struct fcb_httpout httpout;
    struct fcb_pathmerger pathmerger;
    struct fcb_tcpretransmitter tcpretransmitter;
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
extern struct fcb fcbArray[2];

#endif
