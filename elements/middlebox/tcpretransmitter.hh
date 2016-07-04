#ifndef MIDDLEBOX_TCPRETRANSMITTER_HH
#define MIDDLEBOX_TCPRETRANSMITTER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>
#include <click/timestamp.hh>
#include <clicknet/tcp.h>
#include "stackelement.hh"
#include "rbt.hh"
#include "memorypool.hh"
#include "bytestreammaintainer.hh"
#include "tcpretransmissionnode.hh"

CLICK_DECLS

#define RM_TREE_POOL_SIZE 10
#define RM_POOL_SIZE 40
#define RM_PRUNE_THRESHOLD RM_POOL_SIZE / 2

class RBTMemoryPoolRetransmissionManager : public RBTManager
{
public:
    RBTMemoryPoolRetransmissionManager() : poolNodes(RM_POOL_SIZE),
        poolKeys(RM_POOL_SIZE),
        poolInfos(RM_POOL_SIZE),
        poolTrees(RM_TREE_POOL_SIZE)
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
        struct TCPRetransmissionNode* node = (struct TCPRetransmissionNode*)first;
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
        struct TCPRetransmissionNode* node = (struct TCPRetransmissionNode*)info;
        node->~TCPRetransmissionNode();
        poolInfos.releaseMemory(node);
    }

    void freeTree(rb_red_blk_tree* tree)
    {
        poolTrees.releaseMemory(tree);
    }

    uint32_t* allocateKey(void)
    {
        return poolKeys.getMemory();
    }

    struct TCPRetransmissionNode* allocateInfo(void)
    {
        struct TCPRetransmissionNode* node = poolInfos.getMemory();
        // Call the constructor to ensure that the timestamp is clear
        node = new(node) struct TCPRetransmissionNode();
        return node;
    }

private:
    MemoryPool<rb_red_blk_tree> poolTrees;
    MemoryPool<rb_red_blk_node> poolNodes;
    MemoryPool<uint32_t> poolKeys;
    MemoryPool<struct TCPRetransmissionNode> poolInfos;
};

class TCPRetransmitter : public StackElement
{
public:
    TCPRetransmitter() CLICK_COLD;
    ~TCPRetransmitter();

    // Click related methods
    const char *class_name() const        { return "TCPRetransmitter"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }
    void push(int port, Packet *packet);
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    bool isOutElement()                   { return true; }

protected:

private:
    RBTManager* rbtManager;

    void prune(struct fcb *fcb);
    void retransmitSelfAcked(struct fcb *fcb);
};

CLICK_ENDDECLS

#endif
