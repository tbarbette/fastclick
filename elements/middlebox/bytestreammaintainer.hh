/*
 * bytestreammaintainer.hh - Class used to manage a flow. Stores the modifications in it
 * (bytes removed or inserted) as well as information such as the MSS, ports, ips, last
 * ack received, last ack sent, ...
 *
 * This file also contains the declaration and definition of RBTMemoryPoolStreamManager
 * which is the RBT manager used by ByteStreamMaintainer to store the modifications in the flow.
 *
 * Romain Gaillard.
 */


#ifndef MIDDLEBOX_BYTESTREAMMAINTAINER_HH
#define MIDDLEBOX_BYTESTREAMMAINTAINER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include "rbt.hh"
#include "memorypool.hh"

CLICK_DECLS

#define BS_TREE_POOL_SIZE 10
#define BS_POOL_SIZE 5000
#define BS_PRUNE_THRESHOLD 10

/** @class RBTMemoryPoolStreamManager
 * @brief Manager of RBT that uses memory pool to allocate memory and compares
 * the keys as sequence numbers.
 *
 * The keys are sequence numbers (uint32_t)
 * The info are offsets (int)
 */
class RBTMemoryPoolStreamManager : public RBTManager
{
public:
    RBTMemoryPoolStreamManager() : poolNodes(BS_POOL_SIZE),
        poolKeys(BS_POOL_SIZE),
        poolInfos(BS_POOL_SIZE),
        poolTrees(BS_TREE_POOL_SIZE)
    {

    }

    int compareKeys(const void* first, const void* second)
    {
        if(SEQ_GT(*(uint32_t*)first, *(uint32_t*)second))
            return 1;
        if(SEQ_LT(*(uint32_t*)first, *(uint32_t*)second))
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

    rb_red_blk_tree* allocateTree(void)
    {
        return poolTrees.getMemory();
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

    void freeTree(rb_red_blk_tree* tree)
    {
        poolTrees.releaseMemory(tree);
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
    MemoryPool<rb_red_blk_tree> poolTrees;
    MemoryPool<rb_red_blk_node> poolNodes;
    MemoryPool<uint32_t> poolKeys;
    MemoryPool<int> poolInfos;
};

/** @class ByteStreamMaintainer
 * @brief Class used to manage a flow. Stores the modifications in it
 * (bytes removed or inserted) as well as information such as the MSS, ports, ips, last
 * ack received, last ack sent, ...
 *
 * Each side of a connection must have its own ByteStreamMaintainer.
 */
class ByteStreamMaintainer
{
    // ModificationList is the only one allowed to add nodes in the trees
    friend class ModificationList;

    public:
        /** @brief Construct a ByteStreamMaintainer
         * The ByteStreamMaintainer must be initialized before being used
         */
        ByteStreamMaintainer();
        /** @brief Destruct a ByteStreamMaintainer
         */
        ~ByteStreamMaintainer();

        /** @brief Map a ack number
         * @param position Initial ack value
         * @return New value, taking into account the modifications in the flow
         */
        uint32_t mapAck(uint32_t position);

        /** @brief Map a sequence number
         * @param position Initial seq value
         * @return New value, taking into account the modifications in the flow
         */
        uint32_t mapSeq(uint32_t position);

        /** @brief Returns the offset corresponding to the last modification in the ack tree
         * @return The offset with the greatest key in the ack tree or 0 if the tree is empty
         */
        int lastOffsetInAckTree();

        /** @brief Print the ACK and SEQ trees in the console
         */
        void printTrees();

        /** @brief Initialize the ByteStreamMaintainer (required before use)
         * @param rbtManager The RBT Manager used to manage the trees
         * @param flowStart The first sequence number in the flow
         */
        void initialize(RBTMemoryPoolStreamManager *rbtManager, uint32_t flowStart);

        /** @brief Prune the trees
         * @param position Value of the last ACK sent by the destination
         */
        void prune(uint32_t position);

        /** @brief Sets the value of the last ack sent by this side of the connection
         * @param ackNumber Value of the last ack sent by this side of the connection
         */
        void setLastAckSent(uint32_t ackNumber);

        /** @brief Returns the value of the last ack sent by this side of the connection
         * @return Value of the last ack sent by this side of the connection
         */
        uint32_t getLastAckSent();

        /** @brief Indicates whether the last ack sent value has been set.
         * @return True if the last ack sent value has been set
         */
        bool isLastAckSentSet();

        /** @brief Sets the value of the last seq sent by this side of the connection
         * @param seqNumber Value of the last seq sent by this side of the connection
         */
        void setLastSeqSent(uint32_t seqNumber);

        /** @brief Returns the value of the last seq sent by this side of the connection
         * @return Value of the last seq sent by this side of the connection
         */
        uint32_t getLastSeqSent();

        /** @brief Indicates whether the last seq sent value has been set.
         * @return True if the last seq sent value has been set
         */
        bool isLastSeqSentSet();

        /** @brief Sets the value of the last ack received by this side of the connection
         * @param ackNumber Value of the last ack received by this side of the connection
         */
        void setLastAckReceived(uint32_t ackNumber);

        /** @brief Returns the value of the last ack received by this side of the connection
         * @return Value of the last ack received by this side of the connection
         */
        uint32_t getLastAckReceived();

        /** @brief Indicates whether the last ack received value has been set.
         * @return True if the last ack received value has been set
         */
        bool isLastAckReceivedSet();

        /** @brief Return the number of consecutive duplicate ACKs currently observed
         * @return The number of consecutive duplicate ACKs currently observed
         */
        uint8_t getDupAcks();

        /** @brief Set the number of consecutive duplicate ACKs currently observed
         * @param dupAcks The number of consecutive duplicate ACKs currently observed
         */
        void setDupAcks(uint8_t dupAcks);

        /** @brief Set the window size of the source (will be used as received's windows size
         * by the other side of the connection when sending packets)
         * @param windowSize The window size of the source
         */
        void setWindowSize(uint16_t windowSize);

        /** @brief Return the window size of the source (will be used as received's windows size
         * by the other side of the connection when sending packets)
         * @return The window size of the source
         */
        uint16_t getWindowSize();

        /** @brief Set the window scale factor of the source
         * @param windowScale Window scale factor of the source
         */
        void setWindowScale(uint16_t windowScale);

        /** @brief Return the window scale factor of the source
         * @return The window scale factor of the source
         */
        uint16_t getWindowScale();

        /** @brief Indicate whether the connection uses the window scale option
         * @param useWindowScale A boolean indicating whether the connection uses the window
         * scale option
         */
        void setUseWindowScale(bool useWindowScale);

        /** @brief Return a boolean indicating whether the connection uses the window scale option
         * @return A boolean indicating whether the connection uses the window scale option
         */
        bool getUseWindowScale();


        /** @brief Return the MSS of the source
         * @return The mss of the source
         */
        uint16_t getMSS();

        /** @brief Set the MSS of the source
         * @param mss The mss of the source
         */
        void setMSS(uint16_t mss);

        /** @brief Return the size of the congestion window for this side of the connection
         * @return Size of the congestion window for this side of the connection
         */
        uint64_t getCongestionWindowSize();

        /** @brief Set the size of the congestion window for this side of the connection
         * @param congestionWindow Size of the congestion window for this side of the connection
         */
        void setCongestionWindowSize(uint64_t congestionWindow);

        /** @brief Set the ssthreshold (slow start threshold) for this side of the connection
         * @param ssthresh Ssthreshold for this side of the connection
         */
        void setSsthresh(uint64_t ssthresh);

        /** @brief Return the ssthreshold (slow start threshold) for this side of the connection
         * @return Ssthreshold for this side of the connection
         */
        uint64_t getSsthresh();

        /** @brief Set the IP address of the source
         * @param ipSrc IP address of the source
         */
        void setIpSrc(uint32_t ipSrc);

        /** @brief Return the IP address of the source
         * @return IP address of the source
         */
        uint32_t getIpSrc();

        /** @brief Set the IP address of the destination
         * @param ipDst IP address of the destination
         */
        void setIpDst(uint32_t ipDst);

        /** @brief Return the IP address of the destination
         * @return IP address of the destination
         */
        uint32_t getIpDst();

        /** @brief Set the port number of the source
         * @param portSrc port number of the source
         */
        void setPortSrc(uint16_t portSrc);

        /** @brief Return the port number of the source
         * @return port number of the source
         */
        uint16_t getPortSrc();

        /** @brief Set the port number of the destination
         * @param portDst port number of the destination
         */
        void setPortDst(uint16_t portDst);

        /** @brief Return the port number of the destination
         * @return port number of the destination
         */
        uint16_t getPortDst();

    private:
        /** @brief Insert a node in the ACK tree
         * @param position Position of the modification in the flow (key)
         * @param offset Offset of the modification in the flow (info)
         */
        void insertInAckTree(uint32_t position, int offset);

        /** @brief Insert a node in the SEQ tree
         * @param position Position of the modification in the flow (key)
         * @param offset Offset of the modification in the flow (info)
         */
        void insertInSeqTree(uint32_t position, int offset);

        /** @brief Insert a node in the given tree
         * @param tree The tree in which the node will be added
         * @param position Position of the modification in the flow
         * @param offset Offset of the modification in the flow
         */
        void insertInTree(rb_red_blk_tree* tree, uint32_t position, int offset);

        rb_red_blk_tree* treeAck; // Tree used to map the ack numbers
        rb_red_blk_tree* treeSeq; // Tree used to map the sequence numbers
        RBTManager* rbtManager; // Manager of the trees (used to allocate memory inter alia)
        uint32_t pruneCounter; // Used to avoid pruning at each ack received
        bool initialized;
        bool lastAckSentSet; // Indicates whether the value lastAckSent is meaningful
        bool lastSeqSentSet; // Indicates whether the value lastSeqSent is meaningful
        bool lastAckReceivedSet; // Indicates whether the value lastAckReceived is meaningful

        uint32_t lastAckSent;     // /!\ mapped value (as sent)
        uint32_t lastSeqSent;     // /!\ mapped value (as sent)
        uint32_t lastAckReceived; // /!\ Unamapped value (as received)
        uint16_t windowSize; // Window size of the source
        uint16_t windowScale; // Window scale factor of the source
        uint8_t dupAcks;  // Number of consecutive duplicate acks currently observed
        bool useWindowScale; // Indicates if the Window Scale option is used
        uint64_t congestionWindow; // Congestion window for this side of the connection
        uint64_t ssthresh; // Slow start threshold
        uint16_t mss; // MSS of the source
        uint32_t ipSrc;
        uint32_t ipDst;
        uint16_t portSrc;
        uint16_t portDst;
};

CLICK_ENDDECLS
#endif
