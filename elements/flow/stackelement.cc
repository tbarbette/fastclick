#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/packet_anno.hh>
#include "stackelement.hh"


CLICK_DECLS

StackElement::StackElement()
{
    previousStackElement = NULL;
}

StackElement::~StackElement()
{

}

bool StackElement::isStackElement(Element* element)
{
    if(element->cast("StackElement") != NULL)
        return true;
    else
        return false;
}

void* StackElement::cast(const char *name)
{
    if(strcmp(name, "StackElement") == 0)
        return (StackElement*)this;
    else
        return Element::cast(name);
 }

 void StackElement::buildFunctionStack()
 {
     StackVisitor visitor(this);
     this->router()->visit_downstream(this, -1, &visitor);
 }


int StackElement::initialize(ErrorHandler*  errh)
{
    buildFunctionStack();

    return Element::initialize(errh);
}


void StackElement::setAnnotationBit(Packet* p, int bit, bool value) const
{
    // Build masks
    unsigned char clearMask = 1;
    clearMask = clearMask << bit;
    clearMask = ~clearMask;

    unsigned char mask = (unsigned char)value;
    mask = mask << bit;

    // Get previous value
    unsigned char previousValue = (unsigned char)(p->anno_u8(MIDDLEBOX_BOOLS_OFFSET));

    // Apply masks
    // Clear the bit to modify
    previousValue &= clearMask;
    // Set the new value
    previousValue |= mask;

    // Set the new value
    p->set_anno_u8(MIDDLEBOX_BOOLS_OFFSET, previousValue);
}

bool StackElement::getAnnotationBit(Packet* p, int bit) const
{
    // Build mask
    unsigned char mask = 1;
    mask = mask << bit;

    // Get full value
    unsigned char value = (unsigned char)(p->anno_u8(MIDDLEBOX_BOOLS_OFFSET));

    // Apply mask
    value &= mask;

    return (bool)value;
}

void StackElement::setAnnotationLastUseful(Packet *p, bool value) const
{
    setAnnotationBit(p, OFFSET_ANNOTATION_LASTUSEFUL, value);
}

bool StackElement::getAnnotationLastUseful(Packet *p) const
{
    return getAnnotationBit(p, OFFSET_ANNOTATION_LASTUSEFUL);
}

void StackElement::addStackElementInList(StackElement *element, int port)
{
    // Check that this element has not already been added in the list via an
    // alternative path

    previousStackElement = element;
}

void StackElement::setInitialAck(Packet *p, uint32_t initialAck) const
{
    p->set_anno_u32(MIDDLEBOX_INIT_ACK_OFFSET, initialAck);
}

uint32_t StackElement::getInitialAck(Packet *p) const
{
    return (uint32_t)p->anno_u32(MIDDLEBOX_INIT_ACK_OFFSET);
}

void StackElement::closeConnection(Packet *packet, bool graceful)
{
    // Call the "closeConnection" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->closeConnection(packet, graceful);
}

bool StackElement::registerConnectionClose(StackReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt, void* thunk)
{
    // Call the "closeConnection" method on every element in the stack
    if(previousStackElement == NULL)
        return false;

    return previousStackElement->registerConnectionClose(fcb_chain, fnt, thunk);
}

bool StackElement::allowResize() {
    // Call the "removeBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return true;

    previousStackElement->allowResize();
}

void StackElement::removeBytes(WritablePacket* packet, uint32_t position,
    uint32_t length)
{
    // Call the "removeBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->removeBytes(packet, position, length);
}

WritablePacket* StackElement::insertBytes(WritablePacket* packet,
    uint32_t position, uint32_t length)
{
    // Call the "insertBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return NULL;

    return previousStackElement->insertBytes(packet, position, length);
}

void StackElement::requestMorePackets(Packet *packet, bool force)
{
    click_chatter("%p{element} : requestMorePackets", previousStackElement);

    // Call the "requestMorePackets" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->requestMorePackets(packet, force);
}

bool StackElement::isLastUsefulPacket(Packet *packet)
{
    // Call the "isLastUsefulPacket" method on every element in the stack
    if(previousStackElement == NULL)
        return false;

    return previousStackElement->isLastUsefulPacket(packet);
}


unsigned int StackElement::determineFlowDirection()
{
    // Call the "determineFlowDirection" method on every element in the stack
    if(previousStackElement == NULL)
        return -1; // We've reached the end of the path and nobody answered

    return previousStackElement->determineFlowDirection();
}
CLICK_ENDDECLS
EXPORT_ELEMENT(StackElement)
//ELEMENT_MT_SAFE(StackElement)
