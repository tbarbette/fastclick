#ifndef MIDDLEBOX_STACKELEMENT_HH
#define MIDDLEBOX_STACKELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/router.hh>
#include <click/routervisitor.hh>

CLICK_DECLS

class StackElement : public Element
{
public:
    StackElement() CLICK_COLD;

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
    virtual void packetModified(Packet*, int);
    void addStackElementInList(StackElement*);
    static bool isStackElement(Element*);

protected:
    virtual Packet* processPacket(Packet*);
    void setAnnotationModification(Packet*, bool);
    bool getAnnotationModification(Packet*);
    uint32_t getContentOffset(Packet*);
    void setContentOffset(Packet*, uint32_t);
    void modifyPacket(Packet* packet, int);
    void buildFunctionStack();
    const unsigned char* getPacketContentConst(Packet*);
    unsigned char* getPacketContent(WritablePacket*);
    bool isPacketContentEmpty(Packet*);

private:
    struct stackElementListNode *stackElementList;

    // Constants
    const int offsetAnnotation = 12;
    const int offsetContentOffset = 13;

};

struct stackElementListNode
{
    StackElement *node;
    struct stackElementListNode *next;
};

class StackVisitor : public RouterVisitor
{
public:
    StackVisitor(StackElement* startElement)
    {
        this->startElement = startElement;

    }
    ~StackVisitor() {}

    bool visit(Element *e, bool isoutput, int port, Element *from_e, int from_port, int distance)
    {
        if(!StackElement::isStackElement(e))
            return true;

        StackElement *element = (StackElement*)e;
        // Only add stack functions to input elements
        if(element->isOutElement())
            return true;

        click_chatter("Adding element %s to the list of %s", startElement->class_name(), element->class_name());

        element->addStackElementInList(startElement);

        // Stop search when finding the IPOut Element
        if(strcmp(element->class_name(), "IPOut") == 0)
            return false;

        return true;
    }

private:
    StackElement* startElement;
};

CLICK_ENDDECLS

#endif
