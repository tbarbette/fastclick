#ifndef MIDDLEBOX_RETRANSMISSIONMANAGER_HH
#define MIDDLEBOX_RETRANSMISSIONMANAGER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/timestamp.hh>
#include <clicknet/tcp.h>
#include "rbt.hh"
#include "memorypool.hh"
#include "bytestreammaintainer.hh"

#define RM_POOL_SIZE 40
#define RM_PRUNE_THRESHOLD RM_POOL_SIZE / 2
/*
struct RetransmissionNode
{
    Packet* packet;
    Timestamp lastTransmission;

    RetransmissionNode()
    {

    }

    ~RetransmissionNode()
    {

    }
};

class RBTMemoryPoolRetransmissionManager : public RBTManager
{
public:
    RBTMemoryPoolRetransmissionManager() : poolNodes(RM_POOL_SIZE), poolKeys(RM_POOL_SIZE), poolInfos(RM_POOL_SIZE)
    {

    }

    int compareKeys(const void* first, const void* second)
    {
        if(*(uint32_t*)first > *(uint32_t*)second)
            return 1;
        if(*(uint32_t*)first < *(uint32_t*)second)
            return -1;

        return 0;
    }

    void printKey(const void* first)
    {
        click_chatter("%u", *(uint32_t*)first);
    }

    void printInfo(void* first)
    {
        struct RetransmissionNode* node = (struct RetransmissionNode*)first;
        // Get the sequence number of the packet
        const click_tcp *tcph = node->packet->tcp_header();
        uint32_t seq = ntohl(tcph->th_seq);

        // Display the sequence number and timestamp
        click_chatter("Packet %u: %s", seq, node->lastTransmission.unparse().c_str());
    }

    rb_red_blk_node* allocateNode(void)
    {
        return poolNodes.getMemory();
    }

    void freeNode(rb_red_blk_node* node)
    {
        poolNodes.releaseMemory(node);
    }

    void freeKey(void* key)
    {
        poolKeys.releaseMemory((uint32_t*)key);
    }

    void freeInfo(void* info)
    {
        // Call the destructor to allow the timestamp to release its memory
        struct RetransmissionNode* node = (struct RetransmissionNode*)info;
        node->~RetransmissionNode();
        poolInfos.releaseMemory(node);
    }

    uint32_t* allocateKey(void)
    {
        return poolKeys.getMemory();
    }

    struct RetransmissionNode* allocateInfo(void)
    {
        struct RetransmissionNode* node = poolInfos.getMemory();
        // Call the constructor to ensure that the timestamp is clear
        node = new(node) struct RetransmissionNode();
        return node;
    }

private:
    MemoryPool<rb_red_blk_node> poolNodes;
    MemoryPool<uint32_t> poolKeys;
    MemoryPool<struct RetransmissionNode> poolInfos;
};

class RetransmissionManager
{
public:
    RetransmissionManager();
    ~RetransmissionManager();
    bool insertPacket(Packet* packet);
    void ackReceived(uint32_t ackNumber);
    void retransmit(Packet* packet);
    void setByteStreamMaintainer(ByteStreamMaintainer *maintainer);
    void setOutElement(TCPOut *outElement);

private:
    rb_red_blk_tree* tree;
    RBTManager* rbtManager;
    TCPOut *outElement;
    ByteStreamMaintainer *maintainer;
};
*/

#endif
