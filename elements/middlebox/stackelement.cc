#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/packet_anno.hh>
#include "stackelement.hh"

CLICK_DECLS

// Simulation of the Middleclick FCBs
struct fcb fcbArray[2];

StackElement::StackElement()
{
    previousStackElement = NULL;
}

StackElement::~StackElement()
{

}

void StackElement::push(int, Packet *packet)
{
    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    Packet *p = processPacket(&fcbArray[flowDirection], packet);

    if(p != NULL)
        output(0).push(p);
}

Packet* StackElement::pull(int)
{
    Packet *packet = input(0).pull();

    if(packet == NULL)
        return NULL;

    // Similate Middleclick's FCB management
    // We traverse the function stack waiting for TCPIn to give the flow
    // direction.
    unsigned int flowDirection = determineFlowDirection();
    Packet* p = processPacket(&fcbArray[flowDirection], packet);

    return p;
}

Packet* StackElement::processPacket(struct fcb *fcb, Packet* p)
{
    click_chatter("Warning: A stack element has processed a packet in a generic way");

    return p;
}

void StackElement::setContentOffset(Packet* p, uint16_t offset)
{
    p->set_anno_u16(MIDDLEBOX_CONTENTOFFSET_OFFSET, offset);
}

uint16_t StackElement::getContentOffset(Packet* p)
{
    return p->anno_u16(MIDDLEBOX_CONTENTOFFSET_OFFSET);
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
    unsigned char previousValue = (unsigned char)(p->anno_u8(MIDDLEBOX_BOOLS_OFFSET));

    // Apply masks
    // Clear the bit to modify
    previousValue &= clearMask;
    // Set the new value
    previousValue |= mask;

    // Set the new value
    p->set_anno_u8(MIDDLEBOX_BOOLS_OFFSET, previousValue);
}

bool StackElement::getAnnotationBit(Packet* p, int bit)
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

void StackElement::setAnnotationDirty(Packet* p, bool value)
{
    setAnnotationBit(p, offsetAnnotationDirty, value);
}

bool StackElement::getAnnotationDirty(Packet* p)
{
    return getAnnotationBit(p, offsetAnnotationDirty);
}

void StackElement::addStackElementInList(StackElement *element, int port)
{
    // Check that this element was not already added in the list via an
    // alternative path

    previousStackElement = element;
}

void StackElement::setPacketDirty(struct fcb *fcb, WritablePacket* packet)
{
    // Call the "setPacketDirty" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->setPacketDirty(fcb, packet);
}

void StackElement::setInitialAck(Packet *p, uint32_t initialAck)
{
    p->set_anno_u32(MIDDLEBOX_INIT_ACK_OFFSET, initialAck);
}

uint32_t StackElement::getInitialAck(Packet *p)
{
    return (uint32_t)p->anno_u32(MIDDLEBOX_INIT_ACK_OFFSET);
}

void StackElement::packetSent(struct fcb *fcb, Packet* packet)
{
    // Call the "packetSent" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->packetSent(fcb, packet);
}

void StackElement::closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful, bool bothSides)
{
    // Call the "closeConnection" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->closeConnection(fcb, packet, graceful, bothSides);
}

void StackElement::removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    // Call the "removeBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->removeBytes(fcb, packet, position, length);
}

WritablePacket* StackElement::insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    // Call the "insertBytes" method on every element in the stack
    if(previousStackElement == NULL)
        return NULL;

    return previousStackElement->insertBytes(fcb, packet, position, length);
}

void StackElement::requestMorePackets(struct fcb *fcb, Packet *packet)
{
    // Call the "requestMorePackets" method on every element in the stack
    if(previousStackElement == NULL)
        return;

    previousStackElement->requestMorePackets(fcb, packet);
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
