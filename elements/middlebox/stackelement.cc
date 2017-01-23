#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "stackelement.hh"
#include "httpin.hh"

CLICK_DECLS

StackElement::StackElement()
{
    previousStackElement = NULL;
}

StackElement::~StackElement()
{

}

void StackElement::setContentOffset(Packet* p, uint16_t offset)
{
    p->set_anno_u16(offsetContentOffset, offset);
}

uint16_t StackElement::getContentOffset(Packet* p)
{
    return p->anno_u16(offsetContentOffset);
}

void StackElement::setAnnotationBit(Packet* p, int bit, bool value)
{
    // Build masks
    unsigned char clearMask = 1;
    clearMask = clearMask << bit;
    clearMask = ~clearMask;

    unsigned char mask = (unsigned char)value;
    mask = mask << bit;

    // Get previous value
    unsigned char previousValue = (unsigned char)(p->anno_u8(offsetAnnotationBools));

    // Apply masks
    // Clear the bit to modify
    previousValue &= clearMask;
    // Set the new value
    previousValue |= mask;

    // Set the new value
    p->set_anno_u8(offsetAnnotationBools, previousValue);
}

bool StackElement::getAnnotationBit(Packet* p, int bit)
{
    // Build mask
    unsigned char mask = 1;
    mask = mask << bit;

    // Get full value
    unsigned char value = (unsigned char)(p->anno_u8(offsetAnnotationBools));

    // Apply mask
    value &= mask;

    return (bool)value;
}

void StackElement::setAnnotationModification(Packet* p, bool value)
{
    setAnnotationBit(p, offsetAnnotationModified, value);
}

bool StackElement::getAnnotationModification(Packet* p)
{
    return getAnnotationBit(p, offsetAnnotationModified);
}

void StackElement::setAnnotationAcked(Packet* p, bool value)
{
    setAnnotationBit(p, offsetAnnotationAcked, value);
}

bool StackElement::getAnnotationAcked(Packet* p)
{
    return getAnnotationBit(p, offsetAnnotationAcked);
}

void StackElement::addStackElementInList(StackElement *element, int port)
{
    // Check that this element was not already added in the list via an
    // alternative path

    previousStackElement = element;
}

void StackElement::setPacketModified(WritablePacket* packet)
{
    // Call the "setPacketModified" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->setPacketModified(packet);
}

void StackElement::packetSent(Packet* packet)
{
    // Call the "packetSent" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->packetSent(packet);
}

void StackElement::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    // Call the "removeBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->removeBytes(packet, position, length);
}

void StackElement::insertBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    // Call the "insertBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->insertBytes(packet, position, length);
}

void StackElement::requestMoreBytes()
{
    // Call the "requestMoreBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->requestMoreBytes();
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

const unsigned char* StackElement::getPacketContentConst(Packet* p)
{
    uint16_t offset = getContentOffset(p);

    return (p->data() + offset);
}

unsigned char* StackElement::getPacketContent(WritablePacket* p)
{
    uint16_t offset = getContentOffset(p);

    return (p->data() + offset);
}

bool StackElement::isPacketContentEmpty(Packet* packet)
{
    uint16_t offset = getContentOffset(packet);

    if(offset >= packet->length())
        return true;
    else
        return false;
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
