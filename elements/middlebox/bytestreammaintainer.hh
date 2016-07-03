#ifndef MIDDLEBOX_BYTESTREAMMAINTAINER_HH
#define MIDDLEBOX_BYTESTREAMMAINTAINER_HH

#include <click/config.h>
#include <click/glue.hh>
#include "rbt.hh"
#include "memorypool.hh"

CLICK_DECLS

#define BS_POOL_SIZE 40
#define BS_PRUNE_THRESHOLD BS_POOL_SIZE / 2

class RBTMemoryPoolStreamManager : public RBTManager
{
public:
    RBTMemoryPoolStreamManager() : poolNodes(BS_POOL_SIZE), poolKeys(BS_POOL_SIZE), poolInfos(BS_POOL_SIZE)
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
        click_chatter("%d", *(int*)first);
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
        poolInfos.releaseMemory((int*)info);
    }

    uint32_t* allocateKey(void)
    {
        return poolKeys.getMemory();
    }

    int* allocateInfo(void)
    {
        return poolInfos.getMemory();
    }

private:
    MemoryPool<rb_red_blk_node> poolNodes;
    MemoryPool<uint32_t> poolKeys;
    MemoryPool<int> poolInfos;
};

class ByteStreamMaintainer
{
    // ModificationList is the only one allowed to add nodes in the tress
    friend class ModificationList;

    public:
        ByteStreamMaintainer();
        ~ByteStreamMaintainer();
        uint32_t mapAck(uint32_t position);
        uint32_t mapSeq(uint32_t position);
        int lastOffsetInAckTree();
        void setLastAck(uint32_t ackNumber);
        uint32_t getLastAck();
        void setLastSeq(uint32_t seqNumber);
        uint32_t getLastSeq();
        void ackReceived(uint32_t ackNumber);

        void printTrees();

    private:
        void prune(uint32_t position);
        void insertInAckTree(uint32_t position, int offset);
        void insertInSeqTree(uint32_t position, int offset);
        void insertInTree(rb_red_blk_tree* tree, uint32_t position, int offset);

        rb_red_blk_tree* treeAck;
        rb_red_blk_tree* treeSeq;
        RBTManager* rbtManager;
        uint32_t lastAck; // Last ACK value sent
        uint32_t lastSeq; // Last SEQ value sent
        uint32_t pruneCounter;
};

CLICK_ENDDECLS
#endif
