#include <click/config.h>
#include "stackelement.hh"
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

StackElement::StackElement()
{
    stackElementList = NULL;
}

StackElement::~StackElement()
{
    struct stackElementListNode *node = stackElementList;
    struct stackElementListNode *toFree = NULL;

    while(node != NULL)
    {
        toFree = node;
        node = node->next;
        free(toFree);
    }
}

void StackElement::push(int, Packet *packet)
{
    Packet *p = processPacket(packet);
    output(0).push(p);
}

Packet* StackElement::pull(int)
{
    Packet *packet = input(0).pull();
    Packet* p = processPacket(packet);

    return p;
}

Packet* StackElement::processPacket(Packet* p)
{
    click_chatter("Warning: A stack element has processed a packet in a generic way");

    return p;
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

void StackElement::packetModified(Packet* p)
{
    // This function is called when an downstream element modifies a packet
    // By default, it does nothing. Elements can implement their own behavior
}

void StackElement::addStackElementInList(StackElement *element)
{
    struct stackElementListNode *newElement = (struct stackElementListNode *)malloc(sizeof(struct stackElementListNode));

    newElement->node = element;
    newElement->next = stackElementList;

    stackElementList = newElement;
}

void StackElement::modifyPacket(Packet* packet)
{
    // Call the "packetModified" method on every element in the stack
    struct stackElementListNode* current = stackElementList;
    while(current != NULL)
    {
        current->node->packetModified(packet);
        current = current->next;
    }
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
     if(!isOutElement())
     {
         StackVisitor visitor(this);
         this->router()->visit_downstream(this, -1, &visitor);
     }
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

void StackElement::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    unsigned char *source = packet->data();
    uint32_t bytesAfter = packet->length() - position;

    memmove(&source[position], &source[position + length], bytesAfter);
    packet->take(length);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(StackElement)
//ELEMENT_MT_SAFE(StackElement)
