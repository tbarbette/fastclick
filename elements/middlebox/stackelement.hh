#ifndef MIDDLEBOX_STACKELEMENT_HH
#define MIDDLEBOX_STACKELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/router.hh>
#include <click/routervisitor.hh>
#include "fcb.hh"

CLICK_DECLS

class StackElement : public Element
{
public:
    StackElement() CLICK_COLD;
    ~StackElement();

    // Click related methods
    const char *class_name() const        { return "StackElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }
    void* cast(const char*);
    int initialize(ErrorHandler*)   CLICK_COLD;

    void push(int, Packet *);
    Packet *pull(int);

    // Custom methods
    virtual bool isOutElement()          { return false; }
    virtual void addStackElementInList(StackElement* element, int port);
    static bool isStackElement(Element*);

protected:
    friend class PathMerger;

    void setAnnotationModification(Packet*, bool);
    bool getAnnotationModification(Packet*);
    void setAnnotationAcked(Packet* p, bool value);
    bool getAnnotationAcked(Packet* p);
    uint16_t getContentOffset(Packet*);
    void setContentOffset(Packet*, uint16_t);
    void buildFunctionStack();
    const unsigned char* getPacketContentConst(Packet*);
    unsigned char* getPacketContent(WritablePacket*);
    bool isPacketContentEmpty(Packet*);

    virtual Packet* processPacket(struct fcb *fcb, Packet*);

    virtual void setPacketModified(struct fcb *fcb, WritablePacket* packet);
    virtual void removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length);
    virtual WritablePacket* insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void requestMorePackets(struct fcb *fcb, Packet *packet);
    virtual void packetSent(struct fcb *fcb, Packet* packet);
    virtual void closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful);

    // Method used for the simulation of Middleclick's fcb management system
    // Should be removed when integrated to Middleclick
    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    StackElement *previousStackElement;
    void setAnnotationBit(Packet*, int, bool);
    bool getAnnotationBit(Packet*, int);

    // Constants
    const int offsetAnnotationBools = 12; // 1 byte
    const int offsetContentOffset = 13;   // 2 bytes
    const int offsetModificationList = 15; // 4 bytes

    // Up to 8 booleans can be stored in the corresponding annotation
    const int offsetAnnotationModified = 0;
    const int offsetAnnotationAcked = 1;

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
        click_chatter("Adding element %s as predecessor of %s", startElement->class_name(), element->class_name());
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
