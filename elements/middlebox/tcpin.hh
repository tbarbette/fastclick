#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include <click/element.hh>
#include <click/ipflowid.hh>
#include <click/hashtable.hh>
#include <click/memorypool.hh>
#include <click/modificationlist.hh>
#include "tcpclosingstate.hh"
#include "tcpelement.hh"
#include "tcpout.hh"

#define MODIFICATIONLISTS_POOL_SIZE 100
#define MODIFICATIONNODES_POOL_SIZE 200
#define TCPCOMMON_POOL_SIZE 50

CLICK_DECLS

class TCPIn : public TCPElement
{
public:
    TCPIn() CLICK_COLD;
    ~TCPIn();

    const char *class_name() const        { return "TCPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    TCPOut* getOutElement();
    TCPIn* getReturnElement();

    ModificationList* getModificationList(struct fcb*, WritablePacket* packet);
    bool hasModificationList(struct fcb* fcb, Packet* packet);

    struct fcb_tcp_common* getTCPCommon(IPFlowID flowID);

protected:
    virtual Packet* processPacket(struct fcb*, Packet*);

    virtual void setPacketModified(struct fcb*, WritablePacket*);
    virtual void removeBytes(struct fcb*, WritablePacket*, uint32_t, uint32_t);
    virtual WritablePacket* insertBytes(struct fcb*, WritablePacket*, uint32_t, uint32_t);
    virtual void requestMorePackets(struct fcb *fcb, Packet *packet);

    // Method used for the simulation of Middleclick's fcb management system
    // Should be removed when integrated to Middleclick
    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    void closeConnection(struct fcb*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool);
    void closeConnection(struct fcb*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool, bool);
    bool assignTCPCommon(struct fcb *fcb, Packet *packet);
    void ackPacket(struct fcb *fcb, Packet* packet, bool ackMapped);
    Packet* checkClosingConnection(struct fcb *fcb, Packet* packet);

    // TODO Will be thread local as each TCPIn is managed by a different thread
    MemoryPool<struct ModificationNode> poolModificationNodes;
    MemoryPool<struct ModificationList> poolModificationLists;


    // Lock when access these
    HashTable<IPFlowID, struct fcb_tcp_common*> tableFcbTcpCommon;
    MemoryPool<struct fcb_tcp_common> poolFcbTcpCommon;

    TCPOut* outElement;
    TCPIn* returnElement;
};

CLICK_ENDDECLS
#endif
