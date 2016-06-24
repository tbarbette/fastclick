#ifndef MIDDLEBOX_BYTESTREAMMAINTAINER_HH
#define MIDDLEBOX_BYTESTREAMMAINTAINER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/rbt.hh>
#include <click/memorypool.hh>

#define BS_POOL_SIZE 40
#define BS_PRUNE_THRESHOLD BS_POOL_SIZE / 2

class RBTMemoryPoolManager : public RBTManager
{
public:
    RBTMemoryPoolManager() : poolNodes(BS_POOL_SIZE), poolKeys(BS_POOL_SIZE), poolInfos(BS_POOL_SIZE)
    {

    }

    int compareKeys(const void* first, const void* second)
    {
        if(*(unsigned int*)first > *(unsigned int*)second)
            return 1;
        if(*(unsigned int*)first < *(unsigned int*)second)
            return -1;

        return 0;
    }

    void printKey(const void* first)
    {
        click_chatter("%u",*(unsigned int*)first);
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
        poolKeys.releaseMemory((unsigned int*)key);
    }

    void freeInfo(void* info)
    {
        poolInfos.releaseMemory((int*)info);
    }

    unsigned int* allocateKey(void)
    {
        return poolKeys.getMemory();
    }

    int* allocateInfo(void)
    {
        return poolInfos.getMemory();
    }

private:
    MemoryPool<rb_red_blk_node> poolNodes;
    MemoryPool<unsigned int> poolKeys;
    MemoryPool<int> poolInfos;
};

class ByteStreamMaintainer
{
    // ModificationList is the only one allowed to add nodes in the tress
    friend class ModificationList;

    public:
        ByteStreamMaintainer();
        ~ByteStreamMaintainer();
        unsigned int mapAck(unsigned int position);
        unsigned int mapSeq(unsigned int position);
        int lastOffsetInAckTree();
        void printTrees();

    private:
        void prune(unsigned int position);
        void ackReceived(unsigned int position);
        void insertInAckTree(unsigned int position, int offset);
        void insertInSeqTree(unsigned int position, int offset);
        void insertInTree(rb_red_blk_tree* tree, unsigned int position, int offset);

        rb_red_blk_tree* treeAck;
        rb_red_blk_tree* treeSeq;
        RBTManager* rbtManager;
        unsigned int lastAck;
        unsigned int pruneCounter;
};

#endif
