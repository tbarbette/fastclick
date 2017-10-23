#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include <click/element.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>
#include <click/hashtablemp.hh>
#include <click/sync.hh>
#include <click/multithread.hh>
#include <click/memorypool.hh>
#include <click/modificationlist.hh>
#include <click/tcpstate.hh>
#include "retransmissiontiming.hh"
#include "stackelement.hh"
#include "tcpelement.hh"
#include "tcpout.hh"

#define MODIFICATIONLISTS_POOL_SIZE 1000
#define MODIFICATIONNODES_POOL_SIZE 5000
#define TCPCOMMON_POOL_SIZE 2048

#define DEBUG_TCP 0

typedef HashTable<tcp_seq_t, ModificationList*> ModificationTracker;

/**
 * This file is used to simulate the FCB provided by Middleclick
 */

/**
 * Common structure accessed by both sides of a TCP connection.
 * The lock must be acquired before accessing the members of the structure
 */
class tcp_common
{
public:
    // One maintainer for each direction of the connection
    ByteStreamMaintainer maintainers[2];
    // One retransmission manager for each direction of the connection
    RetransmissionTiming retransmissionTimings[2];
    // State of the connection
    TCPState::Value state;
    // Lock to ensure that only one side of the flow (one thread) at a time
    // accesses the common structure
    Spinlock lock;
    int use_count;
    tcp_seq_t lastAckReceived[2] = {0,0};

    tcp_common() : lastAckReceived() //This is indeed called as it is not part of the FCBs
    {
        state = TCPState::ESTABLISHING;
        use_count = 0;
    }

    ~tcp_common()
    {
    }

    inline bool lastAckReceivedSet() {
        return state >= TCPState::OPEN && state < TCPState::CLOSED;
    }

    inline void setLastAckReceived(int direction, tcp_seq_t ackNumber)
    {
        if (SEQ_GT(ackNumber, lastAckReceived[direction]))
            lastAckReceived[direction] = ackNumber;
    }


    tcp_seq_t getLastAckReceived(int direction)
    {
        return lastAckReceived[direction];
    }

};


/**
 * Structure used by the TCPIn element
 */
struct fcb_tcpin : public FlowReleaseChain
{
    tcp_common *common;
    SubFlowRealeaseFnt conn_release_fnt;
    void* conn_release_thunk;
    ModificationTracker* modificationLists;
};


CLICK_DECLS

/*
=c

TCPIn(FLOWDIRECTION, OUTNAME, RETURNNAME)

=s middlebox

entry point of a TCP path in the stack of the middlebox

=d

This element is the entry point of a TCP path in the stack of the middlebox by which all
TCP packets must go before their TCP content is processed. Each path containing a TCPIn element
must also contain a TCPOut element

=item FLOWDIRECTION

ID of the path for the connection (0 or 1). The return path must have the other ID.
Thus, each direction of a TCP connection has a different ID.

=item OUTNAME

Name of the TCPOut element on this path.

=item RETURNNAME

Name of the TCPIn element on the other path of the connection.

=a TCPOut */

class TCPIn : public StackSpaceElement<fcb_tcpin>, public TCPElement
{
public:
    /**
     * @brief Construct a TCPIn element
     */
    TCPIn() CLICK_COLD;

    /**
     * @brief Destruct a TCPIn element
     */
    ~TCPIn() CLICK_COLD;

    const char *class_name() const        { return "TCPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff",FLOW_TCP);

    void push_batch(int port, fcb_tcpin* fcb, PacketBatch* flow);

    /**
     * @brief Return the TCPOut element associated
     * @return A pointer to the TCPOut element associated
     */
    TCPOut* getOutElement();

    /**
     * @brief Return the TCPIn element of the other direction of the connection
     * @return A pointer to the TCPIn element of the other direction of the connection
     */
    TCPIn* getReturnElement();

    /**
     * @brief Return the ModificationList associated to a packet
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A pointer to the ModificationList of the given packet
     */
    ModificationList* getModificationList(WritablePacket* packet);

    /**
     * @brief Indicate whether a packet has a ModificationList associated
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating whether a packet has a ModificationList associated
     */
    bool hasModificationList(Packet* packet);

    /**
     * @brief Return the tcp_common structure stored in the HashTable for this flow
     * @param flowID The IPFlowID corresponding to this flow
     * @return A pointer to the tcp_common structure stored in the HashTable for this flow
     */
    tcp_common* getTCPCommon(IPFlowID flowID);

    inline bool allowResize() {
        return _modification_level >= MODIFICATION_RESIZE;
    }

    /**
     * @brief Return the flow direction
     * @return The flow direction
     */
    inline unsigned int getFlowDirection();

    /**
     * @brief Return the flow direction of the other path
     * @return The flow direction of the other path
     */
    inline unsigned int getOppositeFlowDirection();


    /**
     * @brief Send an ACK for a packet to its source
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @param force A boolean indicating if the ack must be resent even though a similar ack
     * has been sent previously
     */
    void ackPacket(Packet* packet, bool force = false);


protected:
    virtual void removeBytes(WritablePacket*, uint32_t, uint32_t) override;
    virtual WritablePacket* insertBytes(WritablePacket*, uint32_t,
         uint32_t) override CLICK_WARN_UNUSED_RESULT;
    virtual void requestMorePackets(Packet *packet, bool force = false) override;
    virtual void closeConnection(Packet *packet, bool graceful) override;
    virtual bool isLastUsefulPacket(Packet *packet) override;
    virtual unsigned int determineFlowDirection() override;

    /**
     * Remove timeout and clean function
     */
    void releaseFCBState();

    /**
     * @brief Set the flow direction
     * @param flowDirection The flow direction
     */
    void setFlowDirection(unsigned int flowDirection);



private:

    /**
     * @brief Function called when the timeout expire or when all packets are
     * released. Will call the release chain.
     */
    static void release_tcp(FlowControlBlock* fcb, void* thunk);

    /**
     * Function to release internal flow state, called by release_tcp but do not call the FCB release chain. But do call the Stack release chain
     */
    void release_tcp_internal(FlowControlBlock* fcb);

    bool registerConnectionClose(StackReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt, void* thunk) override;

    /**
     * @brief Assign a tcp_common structure in the FCB of this flow. If the given packet
     * is a SYN packet, it will allocate a structure and set the pointer in the fcb.
     * If the packet is a SYNACK packet, it will get the structure allocated by the other direction
     * of the connection (when it received the SYN packet) and set the pointer in the fcb
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating whether the structure has been assigned
     */
    bool assignTCPCommon(Packet *packet);

    /**
     * @brief Check whether the connection has been closed or not
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating if the connection is still open
     */
    bool checkConnectionClosed(Packet *packet);

    /**
     * @brief Manage the TCP options. It will disable the SACK-permitted option if needed,
     * get the MSS, and set the window scale factor if needed.
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     */
    void manageOptions(WritablePacket *packet);

    per_thread<MemoryPool<struct ModificationNode>> poolModificationNodes;
    per_thread<MemoryPool<struct ModificationList>> poolModificationLists;
    per_thread<RBTMemoryPoolStreamManager> rbtManager;

    HashTableMP<IPFlowID, tcp_common*> tableFcbTcpCommon;
    pool_allocator_mt<tcp_common,false,TCPCOMMON_POOL_SIZE> poolFcbTcpCommon;
    pool_allocator_mt<ModificationTracker,false> poolModificationTracker;

    TCPOut* outElement; // TCPOut element of this path
    TCPIn* returnElement; // TCPIn element of the return path
    unsigned int flowDirection;

    int _modification_level;
    bool _verbose;
    friend class TCPOut;
};

inline unsigned int TCPIn::getFlowDirection()
{
    return flowDirection;
}


inline unsigned int TCPIn::getOppositeFlowDirection()
{
    return (1 - flowDirection);
}


CLICK_ENDDECLS
#endif
