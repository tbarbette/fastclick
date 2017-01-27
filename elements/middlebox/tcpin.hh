#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include <click/element.hh>
#include <click/hashtable.hh>
#include <click/memorypool.hh>
#include <click/modificationlist.hh>
#include "tcpclosingstate.hh"
#include "tcpelement.hh"
#include "tcpout.hh"
#include <click/modificationlist.hh>
#include <clicknet/tcp.h>
#include <click/hashtable.hh>
#include <click/bytestreammaintainer.hh>

#include <click/memorypool.hh>
#include "tcpclosingstate.hh"
#include "tcpreordernode.hh"

#define MODIFICATIONLISTS_POOL_SIZE 100
#define MODIFICATIONNODES_POOL_SIZE 200

CLICK_DECLS


class fcb_tcpin
{
public:
    HashTable<tcp_seq_t, ModificationList*> modificationLists;
    TCPClosingState::Value closingState;
    MemoryPool<struct ModificationList>* poolModificationLists;
    MemoryPool<struct ModificationNode>* poolModificationNodes;

    //TODO : ALl this will be ignored
    fcb_tcpin() : modificationLists(NULL)
    {
        closingState = TCPClosingState::OPEN;
        poolModificationLists = NULL;
        poolModificationNodes = NULL;
    }

    ~fcb_tcpin()
    {
        // Put back in the corresponding memory pool all the modification lists
        // in use (in the hashtable)
        for(HashTable<tcp_seq_t, ModificationList*>::iterator it = modificationLists.begin();
            it != modificationLists.end(); ++it)
        {
            // Call the destructor to release object's own memory
            (it.value())->~ModificationList();
            // Put it back in the pool
            poolModificationLists->releaseMemory(it.value());
        }
    }

    ByteStreamMaintainer maintainers[2];
};



class TCPIn : public StackBufferElement<fcb_tcpin>,TCPElement
{
public:
    TCPIn() CLICK_COLD;
    ~TCPIn();

    const char *class_name() const        { return "TCPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    virtual void push_batch(int,fcb_tcpin*,PacketBatch*) override;

    TCPOut* getOutElement();
    TCPIn* getReturnElement();

    ModificationList* getModificationList(fcb_tcpin*, WritablePacket* packet);
    bool hasModificationList(fcb_tcpin* fcb, Packet* packet);

protected:


    virtual void setPacketModified(WritablePacket*) override;
    virtual void removeBytes(WritablePacket*, uint32_t, uint32_t) override;
    virtual void insertBytes(WritablePacket*, uint32_t, uint32_t) override;
    virtual void requestMoreBytes() override;

    // Method used for the simulation of Middleclick's fcb management system
    // Should be removed when integrated to Middleclick
    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    void closeConnection(fcb_tcpin*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool);
    void closeConnection(fcb_tcpin*, uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool, bool);

    // TODO Will be thread local as each TCPIn is managed by a different thread
    MemoryPool<struct ModificationNode> poolModificationNodes;
    MemoryPool<struct ModificationList> poolModificationLists;
    TCPOut* outElement;
    TCPIn* returnElement;

    friend class TCPOut;
};

CLICK_ENDDECLS
#endif
