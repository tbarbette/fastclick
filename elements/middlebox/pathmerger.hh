#ifndef MIDDLEBOX_PATHMERGER_HH
#define MIDDLEBOX_PATHMERGER_HH
#include <click/element.hh>
#include "stackelement.hh"
/*

class fcb_pathmerger
{
    HashTable<tcp_seq_t, int> portMap;

    fcb_pathmerger() : portMap(-1)
    {
    }
};


CLICK_DECLS

class PathMerger : public StackElement
{
public:
    PathMerger() CLICK_COLD;
    ~PathMerger();

    const char *class_name() const        { return "PathMerger"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    virtual void addStackElementInList(StackElement* element, int port);

protected:
    virtual void setPacketModified(WritablePacket* packet);
    virtual void removeBytes(WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void insertBytes(WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void requestMoreBytes();
    virtual void packetSent(Packet* packet);

private:
    StackElement* previousStackElements[2];

    int getPortForPacket(Packet *packet);
    void setPortForPacket(Packet *packet, int port);
    StackElement* getElementForPacket(Packet* packet);
    void removeEntry(Packet* packet);
    void addEntry(Packet* packet, int port);
};*/

CLICK_ENDDECLS

#endif
