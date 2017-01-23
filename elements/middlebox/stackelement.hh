#ifndef MIDDLEBOX_STACKELEMENT_HH
#define MIDDLEBOX_STACKELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/flowelement.hh>
#include <click/router.hh>
#include <click/routervisitor.hh>


CLICK_DECLS

class StackElement : public VirtualFlowBufferElement
{
public:
    StackElement() CLICK_COLD;
    ~StackElement();

    // Click related methods
    const char *class_name() const        { return "StackElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return "h/hh"; }
    void* cast(const char*);
    int initialize(ErrorHandler*)   CLICK_COLD;

    // Custom methods
    virtual bool isOutElement()          { return false; }
    virtual void addStackElementInList(StackElement* element, int port);
    static bool isStackElement(Element*);


    static void setAnnotationModification(Packet*, bool);
    static bool getAnnotationModification(Packet*);

    virtual const size_t flow_data_size()  const { return 0; }

protected:
    friend class PathMerger;
    

    void setAnnotationAcked(Packet* p, bool value);
    bool getAnnotationAcked(Packet* p);
    uint16_t getContentOffset(Packet*);
    void setContentOffset(Packet*, uint16_t);
    void buildFunctionStack();
    const unsigned char* getPacketContentConst(Packet*);
    unsigned char* getPacketContent(WritablePacket*);
    bool isPacketContentEmpty(Packet*);

    virtual void setPacketModified(WritablePacket* packet);
    virtual void removeBytes(WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void insertBytes(WritablePacket* packet, uint32_t position, uint32_t length);
    virtual void requestMoreBytes();
    virtual void packetSent(Packet* packet);

    // Method used for the simulation of Middleclick's fcb management system
    // Should be removed when integrated to Middleclick
    // This method process the stack function until an element is able to
    // answer the question
    virtual unsigned int determineFlowDirection();

private:
    StackElement *previousStackElement;
    static void setAnnotationBit(Packet*, int, bool);
    static bool getAnnotationBit(Packet*, int);

    // Constants
    static const int offsetAnnotationBools = 12; // 1 byte
    static const int offsetContentOffset = 13;   // 2 bytes
    static const int offsetModificationList = 15; // 4 bytes

    // Up to 8 booleans can be stored in the corresponding annotation
    static const int offsetAnnotationModified = 0;
    static const int offsetAnnotationAcked = 1;

};

template<typename T>
class StackBufferElement : public StackElement {
public :
    StackBufferElement() : StackElement() {

    }

    virtual int initialize(ErrorHandler *errh) {
        if (_flow_data_offset == -1) {
            return errh->error("No SFCBAssigner() element sets the flow context for %s !",name().c_str());
        }
        return 0;
    }

    virtual const size_t flow_data_size()  const { return sizeof(T); }



    inline T* fcb() {
        T* flowdata = static_cast<T*>((void*)&fcb_stack->data[_flow_data_offset]);
        return flowdata;
    }

    void push_batch(int port,PacketBatch* head) final {
        push_batch(port, fcb(), head);
    };

    virtual void push_batch(int port, T* flowdata, PacketBatch* head) = 0;

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
