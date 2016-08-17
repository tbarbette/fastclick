#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include <click/element.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>
#include <click/sync.hh>
#include <click/multithread.hh>
#include "memorypool.hh"
#include "modificationlist.hh"
#include "tcpclosingstate.hh"
#include "stackelement.hh"
#include "tcpelement.hh"
#include "tcpout.hh"

#define MODIFICATIONLISTS_POOL_SIZE 1000
#define MODIFICATIONNODES_POOL_SIZE 5000
#define TCPCOMMON_POOL_SIZE 50

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

class TCPIn : public StackElement, public TCPElement
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
    ModificationList* getModificationList(struct fcb* fcb, WritablePacket* packet);

    /**
     * @brief Indicate whether a packet has a ModificationList associated
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating whether a packet has a ModificationList associated
     */
    bool hasModificationList(struct fcb* fcb, Packet* packet);

    /**
     * @brief Return the fcb_tcp_common structure stored in the HashTable for this flow
     * @param flowID The IPFlowID corresponding to this flow
     * @return A pointer to the fcb_tcp_common structure stored in the HashTable for this flow
     */
    struct fcb_tcp_common* getTCPCommon(IPFlowID flowID);

protected:
    virtual Packet* processPacket(struct fcb*, Packet*);

    virtual void removeBytes(struct fcb*, WritablePacket*, uint32_t, uint32_t);
    virtual WritablePacket* insertBytes(struct fcb*, WritablePacket*, uint32_t,
         uint32_t) CLICK_WARN_UNUSED_RESULT;
    virtual void requestMorePackets(struct fcb *fcb, Packet *packet, bool force = false);
    virtual void closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful,
         bool bothSides);
    virtual bool isLastUsefulPacket(struct fcb* fcb, Packet *packet);
    virtual unsigned int determineFlowDirection();

    /**
     * @brief Set the flow direction
     * @param flowDirection The flow direction
     */
    void setFlowDirection(unsigned int flowDirection);

    /**
     * @brief Return the flow direction
     * @return The flow direction
     */
    unsigned int getFlowDirection();

    /**
     * @brief Return the flow direction of the other path
     * @return The flow direction of the other path
     */
    unsigned int getOppositeFlowDirection();

private:
    /**
     * @brief Assign a tcp_common structure in the FCB of this flow. If the given packet
     * is a SYN packet, it will allocate a structure and set the pointer in the fcb.
     * If the packet is a SYNACK packet, it will get the structure allocated by the other direction
     * of the connection (when it received the SYN packet) and set the pointer in the fcb
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating whether the structure has been assigned
     */
    bool assignTCPCommon(struct fcb *fcb, Packet *packet);

    /**
     * @brief Send an ACK for a packet to its source
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @param force A boolean indicating if the ack must be resent even though a similar ack
     * has been sent previously
     */
    void ackPacket(struct fcb *fcb, Packet* packet, bool force = false);

    /**
     * @brief Check whether the connection has been closed or not
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     * @return A boolean indicating if the connection is still open
     */
    bool checkConnectionClosed(struct fcb* fcb, Packet *packet);

    /**
     * @brief Manage the TCP options. It will disable the SACK-permitted option if needed,
     * get the MSS, and set the window scale factor if needed.
     * @param fcb A pointer to the FCB of this flow
     * @param packet The packet
     */
    void manageOptions(struct fcb* fcb, WritablePacket *packet);

    per_thread<MemoryPool<struct ModificationNode>> poolModificationNodes;
    per_thread<MemoryPool<struct ModificationList>> poolModificationLists;
    per_thread<RBTMemoryPoolStreamManager> rbtManager;

    Spinlock lock; // Lock to access to the two elements below
    HashTable<IPFlowID, struct fcb_tcp_common*> tableFcbTcpCommon;
    MemoryPool<struct fcb_tcp_common> poolFcbTcpCommon;

    TCPOut* outElement; // TCPOut element of this path
    TCPIn* returnElement; // TCPIn element of the return path
    unsigned int flowDirection;
};

CLICK_ENDDECLS
#endif
