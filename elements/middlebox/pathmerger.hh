#ifndef MIDDLEBOX_PATHMERGER_HH
#define MIDDLEBOX_PATHMERGER_HH
#include <click/element.hh>
#include "stackelement.hh"

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

    void push(int port, Packet *packet);

    virtual void addStackElementInList(StackElement* element, int port);

protected:
    virtual void setPacketModified(struct fcb *fcb, WritablePacket* packet);
    virtual void removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void requestMoreBytes(struct fcb *fcb);
    virtual void packetSent(struct fcb *fcb, Packet* packet);

private:
    StackElement* previousStackElements[2];

    int getPortForPacket(struct fcb *fcb, Packet *packet);
    void setPortForPacket(struct fcb *fcb, Packet *packet, int port);
    StackElement* getElementForPacket(struct fcb *fcb, Packet* packet);
    void removeEntry(struct fcb *fcb, Packet* packet);
    void addEntry(struct fcb *fcb, Packet* packet, int port);
};

CLICK_ENDDECLS

#endif
