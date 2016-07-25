#ifndef MIDDLEBOX_STACKELEMENT_HH
#define MIDDLEBOX_STACKELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/batchelement.hh>
#include "batchfcb.hh"
#include <click/router.hh>
#include <click/routervisitor.hh>
#include "fcb.hh"

CLICK_DECLS

class StackElement : public BatchElement
{
public:
    friend class PathMerger;
    friend class FlowBuffer;
    friend class FlowBufferContentIter;

    StackElement() CLICK_COLD;
    ~StackElement();

    // Click related methods
    const char *class_name() const        { return "StackElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }
    void* cast(const char*);
    int initialize(ErrorHandler*)   CLICK_COLD;

    void push_packet(int, Packet *);
    Packet *pull(int);

    #if HAVE_BATCH
    void push_batch(int, PacketBatch *batch);
    PacketBatch* pull_batch(int port, int max);
    #endif

    // Custom methods
    virtual bool isOutElement()          { return false; }
    virtual void addStackElementInList(StackElement* element, int port);
    static bool isStackElement(Element*);

protected:
    void setAnnotationLastUseful(Packet*, bool) const;
    bool getAnnotationLastUseful(Packet*) const;
    uint16_t getContentOffset(Packet*) const;
    const unsigned char* getPacketContentConst(Packet*) const;
    unsigned char* getPacketContent(WritablePacket*) const;
    void setContentOffset(Packet*, uint16_t) const;
    void setInitialAck(Packet *p, uint32_t initialAck) const;
    uint32_t getInitialAck(Packet *p) const;
    bool isPacketContentEmpty(Packet*) const;
    uint16_t getPacketContentSize(Packet *packet) const;

    void buildFunctionStack();

    virtual Packet* processPacket(struct fcb *fcb, Packet*);

    virtual void removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position,
        uint32_t length);
    virtual WritablePacket* insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position,
        uint32_t length) CLICK_WARN_UNUSED_RESULT;
    virtual void requestMorePackets(struct fcb *fcb, Packet *packet, bool force = false);
    virtual void packetSent(struct fcb *fcb, Packet* packet);
    virtual void closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful,
        bool bothSides);
    virtual bool isLastUsefulPacket(struct fcb* fcb, Packet *packet);

    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    StackElement *previousStackElement;
    void setAnnotationBit(Packet*, int, bool) const;
    bool getAnnotationBit(Packet*, int) const;

    // Constants
    // Up to 8 booleans can be stored in the corresponding annotation
    const int OFFSET_ANNOTATION_LASTUSEFUL = 0;

};

class StackVisitor : public RouterVisitor
{
public:
    StackVisitor(StackElement* startElement)
    {
        this->startElement = startElement;
    }
    ~StackVisitor()
    {

    }
    bool visit(Element *e, bool isoutput, int port, Element *from_e, int from_port, int distance)
    {
        // Check that the element is a stack element
        // If this is not the case, we skip it and continue the traversing
        if(!StackElement::isStackElement(e))
            return true;

        // We now know that we have a stack element so we can cast it
        StackElement *element = reinterpret_cast<StackElement*>(e);

        // Add the starting element in the list of the current element
        click_chatter("Adding element %s as predecessor of %s", startElement->class_name(),
            element->class_name());
        element->addStackElementInList(startElement, port);

        // Stop search when we encounter the IPOut Element
        if(strcmp(element->class_name(), "IPOut") == 0)
            return false;

        // Stop the traversing
        return false;
    }

private:
    StackElement* startElement;
};

CLICK_ENDDECLS

#endif
