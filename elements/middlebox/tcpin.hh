#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include <click/element.hh>
#include <click/hashtable.hh>
#include <click/memorypool.hh>
#include <click/modificationlist.hh>
#include "tcpclosingstate.hh"
#include "tcpelement.hh"
#include "tcpout.hh"

#define MODIFICATIONLISTS_POOL_SIZE 100
#define MODIFICATIONNODES_POOL_SIZE 200

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

protected:
    virtual Packet* processPacket(struct fcb*, Packet*);

    virtual void setPacketModified(struct fcb*, WritablePacket*);
    virtual void removeBytes(struct fcb*, WritablePacket*, uint32_t, uint32_t);
    virtual void insertBytes(struct fcb*, WritablePacket*, uint32_t, uint32_t);
    virtual void requestMoreBytes(struct fcb*);

    // Method used for the simulation of Middleclick's fcb management system
    // Should be removed when integrated to Middleclick
    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    void closeConnection(struct fcb*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool);
    void closeConnection(struct fcb*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool, bool);

    // TODO Will be thread local as each TCPIn is managed by a different thread
    MemoryPool<struct ModificationNode> poolModificationNodes;
    MemoryPool<struct ModificationList> poolModificationLists;
    TCPOut* outElement;
    TCPIn* returnElement;
};

CLICK_ENDDECLS
#endif
