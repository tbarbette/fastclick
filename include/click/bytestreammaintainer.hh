/*
 * bytestreammaintainer.hh - Class used to manage a flow. Stores the modifications in the flow
 * (bytes removed or inserted) as well as information such as the MSS, ports, ips, last
 * ack received, last ack sent, ...
 *
 * This file also contains the declaration and definition of RBTManager
 * which is the RBTManager used by ByteStreamMaintainer to store the modifications in the flow.
 *
 * Romain Gaillard.
 */


#ifndef MIDDLEBOX_BYTESTREAMMAINTAINER_HH
#define MIDDLEBOX_BYTESTREAMMAINTAINER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include "rbt.hh"

CLICK_DECLS

/** @class ByteStreamMaintainer
 * @brief Class used to manage a flow. Stores the modifications in the flow
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
         * ByteStreamMaintainer must be initialized before being used
         */
        ByteStreamMaintainer();
        /** @brief Destruct a ByteStreamMaintainer
         */
        ~ByteStreamMaintainer();

        inline void reinit() {
		this->~ByteStreamMaintainer();
		new (this) ByteStreamMaintainer();
        }

        /** @brief Map an ack number
         * @param position Initial ack value
         * @return New value, taking into account the modifications in the flow
         */
        uint32_t mapAck(uint32_t position);

        /** @brief Map a sequence number
         * @param position Initial seq value
         * @return New value, taking into account the modifications in the flow
         */
        uint32_t mapSeq(uint32_t position);

        /** @brief Return the offset corresponding to the last modification in the ack tree
         * @return The offset with the greatest key in the ack tree or 0 if the tree is empty
         */
        int lastOffsetInAckTree();

        /** @brief Print the ACK and SEQ trees in the console
         */
        void printTrees();

        /** @brief Initialize the ByteStreamMaintainer (required before beging use)
         * @param rbtManager The RBTManager used to manage the trees
         * @param flowStart The first sequence number in the flow
         */
        void initialize(RBTManager *rbtManager, uint32_t flowStart);

        /** @brief Prune the trees
         * @param position Value of the last ACK sent by the destination
         */
        void prune(uint32_t position);

        /** @brief Sets the value of the last ack sent by this side of the connection
         * @param ackNumber Value of the last ack sent by this side of the connection
         */
        inline void setLastAckSent(uint32_t ackNumber);

        /** @brief Return the value of the last ack sent by this side of the connection
         * @return Value of the last ack sent by this side of the connection
         */
        inline uint32_t getLastAckSent();

        /** @brief Indicate whether the "last ack sent" value has been set.
         * @return True if the "last ack sent" value has been set
         */
        inline bool isLastAckSentSet();

        /** @brief Set the value of the "last seq sent" by this side of the connection
         * @param seqNumber Value of the "last seq sent" by this side of the connection
         */
        inline void setLastSeqSent(uint32_t seqNumber);

        /** @brief Set the size of the last packet payload, allows to compute next seq
         */
        inline void setLastPayloadLength(uint16_t payload);

        /** @brief Return the value of the "last seq sent" by this side of the connection
         * @return Value of the "last seq sent" by this side of the connection
         */
        inline uint32_t getLastSeqSent();

        inline uint16_t getLastPayloadLength();

        /** @brief Indicate whether the "last seq sent" value has been set.
         * @return True if the "last seq sent" value has been set
         */
        inline bool isLastSeqSentSet();

        /** @brief Return the number of consecutive duplicate ACKs currently observed
         * @return The number of consecutive duplicate ACKs currently observed
         */
        inline uint8_t getDupAcks();

        /** @brief Set the number of consecutive duplicate ACKs currently observed
         * @param dupAcks The number of consecutive duplicate ACKs currently observed
         */
        inline void setDupAcks(uint8_t dupAcks);

        /** @brief Set the window size of the source (will be used as the receiver's windows size
         * by the other side of the connection when sending packets)
         * @param windowSize The window size of the source
         */
        inline void setWindowSize(uint16_t windowSize);

        /** @brief Return the window size of the source (will be used as the receiver's windows size
         * by the other side of the connection when sending packets)
         * @return The window size of the source
         */
        inline uint16_t getWindowSize();

        /** @brief Set the window scale factor of the source
         * @param windowScale Window scale factor of the source
         */
        inline void setWindowScale(uint16_t windowScale);

        /** @brief Return the window scale factor of the source
         * @return The window scale factor of the source
         */
        inline uint16_t getWindowScale();

        /** @brief Indicate whether the connection uses the window scale option
         * @param useWindowScale A boolean indicating whether the connection uses the window
         * scale option
         */
        inline void setUseWindowScale(bool useWindowScale);

        /** @brief Return a boolean indicating whether the connection uses the window scale option
         * @return A boolean indicating whether the connection uses the window scale option
         */
        inline bool getUseWindowScale();

        /** @brief Return the MSS of the source
         * @return The mss of the source
         */
        inline uint16_t getMSS();

        /** @brief Set the MSS of the source
         * @param mss The mss of the source
         */
        inline void setMSS(uint16_t mss);

        /** @brief Return the size of the congestion window for this side of the connection
         * @return Size of the congestion window for this side of the connection
         */
        inline uint64_t getCongestionWindowSize();

        /** @brief Set the size of the congestion window for this side of the connection
         * @param congestionWindow Size of the congestion window for this side of the connection
         */
        inline void setCongestionWindowSize(uint64_t congestionWindow);

        /** @brief Set the ssthreshold (slow start threshold) for this side of the connection
         * @param ssthresh Ssthreshold for this side of the connection
         */
        inline void setSsthresh(uint64_t ssthresh);

        /** @brief Return the ssthreshold (slow start threshold) for this side of the connection
         * @return Ssthreshold for this side of the connection
         */
        inline uint64_t getSsthresh();

        /** @brief Set the IP address of the source
         * @param ipSrc IP address of the source
         */
        inline void setIpSrc(uint32_t ipSrc);

        /** @brief Return the IP address of the source
         * @return IP address of the source
         */
        inline uint32_t getIpSrc();

        /** @brief Set the IP address of the destination
         * @param ipDst IP address of the destination
         */
        inline void setIpDst(uint32_t ipDst);

        /** @brief Return the IP address of the destination
         * @return IP address of the destination
         */
        inline uint32_t getIpDst();

        /** @brief Set the port number of the source
         * @param portSrc port number of the source
         */
        inline void setPortSrc(uint16_t portSrc);

        /** @brief Return the port number of the source
         * @return port number of the source
         */
        inline uint16_t getPortSrc();

        /** @brief Set the port number of the destination
         * @param portDst port number of the destination
         */
        inline void setPortDst(uint16_t portDst);

        /** @brief Return the port number of the destination
         * @return port number of the destination
         */
        inline uint16_t getPortDst();

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
        RBTManager* rbtManager; // Manager of the trees (used to allocate memory among others)
        uint32_t pruneCounter; // Used to avoid pruning for each ack received
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
        uint16_t lastPayloadLength;
        uint32_t ipSrc;
        uint32_t ipDst;
        uint16_t portSrc;
        uint16_t portDst;
};

void ByteStreamMaintainer::setLastAckSent(uint32_t ackNumber)
{
    // As the sequence and ack numbers may wrap, we cannot just set a default value (for instance
    // 0) for them and check that the given one is greater as we could have false negatives
    if(!lastAckSentSet || SEQ_GT(ackNumber, lastAckSent))
        lastAckSent = ackNumber;

    lastAckSentSet = true;
}

uint32_t ByteStreamMaintainer::getLastAckSent()
{
    if(!lastAckSentSet)
        click_chatter("Error: Last ack sent not defined");

    return lastAckSent;
}

void ByteStreamMaintainer::setLastSeqSent(uint32_t seqNumber)
{
    // As the sequence and ack numbers may wrap, we cannot just set a default value (for instance
    // 0) for them and check that the given one is greater as we could have false negatives
    if(!lastSeqSentSet || SEQ_GT(seqNumber, lastSeqSent))
        lastSeqSent = seqNumber;

    lastSeqSentSet = true;
}

void ByteStreamMaintainer::setLastPayloadLength(uint16_t payload)
{
    lastPayloadLength = payload;
}

uint32_t ByteStreamMaintainer::getLastSeqSent()
{
    if(!lastSeqSentSet)
        click_chatter("Error: Last sequence sent not defined");

    return lastSeqSent;
}

uint16_t ByteStreamMaintainer::getLastPayloadLength()
{
    return lastPayloadLength;
}

bool ByteStreamMaintainer::isLastAckSentSet()
{
    return lastAckSentSet;
}

bool ByteStreamMaintainer::isLastSeqSentSet()
{
    return lastSeqSentSet;
}

void ByteStreamMaintainer::setWindowSize(uint16_t windowSize)
{
    this->windowSize = windowSize;
}

uint16_t ByteStreamMaintainer::getWindowSize()
{
    return windowSize;
}

void ByteStreamMaintainer::setIpSrc(uint32_t ipSrc)
{
    this->ipSrc = ipSrc;
}

uint32_t ByteStreamMaintainer::getIpSrc()
{
    return ipSrc;
}

void ByteStreamMaintainer::setIpDst(uint32_t ipDst)
{
    this->ipDst = ipDst;
}

uint32_t ByteStreamMaintainer::getIpDst()
{
    return ipDst;
}

void ByteStreamMaintainer::setPortSrc(uint16_t portSrc)
{
    this->portSrc = portSrc;
}

uint16_t ByteStreamMaintainer::getPortSrc()
{
    return portSrc;
}

void ByteStreamMaintainer::setPortDst(uint16_t portDst)
{
    this->portDst = portDst;
}

uint16_t ByteStreamMaintainer::getPortDst()
{
    return portDst;
}

void ByteStreamMaintainer::setWindowScale(uint16_t windowScale)
{
    this->windowScale = windowScale;
}

uint16_t ByteStreamMaintainer::getWindowScale()
{
    return windowScale;
}

void ByteStreamMaintainer::setUseWindowScale(bool useWindowScale)
{
    this->useWindowScale = useWindowScale;
}

bool ByteStreamMaintainer::getUseWindowScale()
{
    return useWindowScale;
}

uint16_t ByteStreamMaintainer::getMSS()
{
    return mss;
}

void ByteStreamMaintainer::setMSS(uint16_t mss)
{
    this->mss = mss;
}

uint8_t ByteStreamMaintainer::getDupAcks()
{
    return dupAcks;
}

void ByteStreamMaintainer::setDupAcks(uint8_t dupAcks)
{
    this->dupAcks = dupAcks;
}

uint64_t ByteStreamMaintainer::getCongestionWindowSize()
{
    return congestionWindow;
}

void ByteStreamMaintainer::setCongestionWindowSize(uint64_t congestionWindow)
{
    this->congestionWindow = congestionWindow;
}

uint64_t ByteStreamMaintainer::getSsthresh()
{
    return ssthresh;
}

void ByteStreamMaintainer::setSsthresh(uint64_t ssthresh)
{
    this->ssthresh = ssthresh;
}


CLICK_ENDDECLS
#endif
