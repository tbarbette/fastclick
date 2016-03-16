#ifndef MIDDLEBOX_BYTESTREAMMAINTAINER_HH
#define MIDDLEBOX_BYTESTREAMMAINTAINER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/rbt.hh>
#include <click/memorypool.hh>

#define BS_POOL_SIZE 20
#define BS_PRUNE_THRESHOLD BS_POOL_SIZE / 2

class RBTMemoryPoolManager : public RBTManager
{
public:
    RBTMemoryPoolManager() : poolNodes(BS_POOL_SIZE), poolKeys(BS_POOL_SIZE * 2), poolInfos(BS_POOL_SIZE * 2)
    {

    }

    int compareKeys(const void* first, const void* second)
    {
        if(*(int*)first > *(int*)second)
            return 1;
        if(*(int*)first < *(int*)second)
            return -1;

        return 0;
    }

    void printKey(const void* first)
    {
        click_chatter("%u",*(unsigned int*)first);
    }

    void printInfo(void* first)
    {
        click_chatter("%i", *(int*)first);
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
    public:
        ByteStreamMaintainer();
        ~ByteStreamMaintainer();
        void newInsertion(unsigned int, int);
        void newDeletion(unsigned int, int);
        int getAckOffset(unsigned int);
        int getSeqOffset();
        void ackReceived(unsigned int);
        void printTree();

    private:
        void prune(unsigned int);

        int seqOffset;
        rb_red_blk_tree* tree;
        RBTManager* rbtManager;
        unsigned int lastAck;
        unsigned int pruneCounter;
};

#endif
