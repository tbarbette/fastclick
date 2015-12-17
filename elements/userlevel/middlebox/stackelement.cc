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

void StackElement::setContentOffset(Packet* p, uint32_t offset)
{
    p->set_anno_u32(offsetContentOffset, offset);
}

uint32_t StackElement::getContentOffset(Packet* p)
{
    return (int)(p->anno_u32(offsetContentOffset));
}

void StackElement::setAnnotationModification(Packet* p, bool value)
{
    p->set_anno_u8(offsetAnnotation, (uint8_t)value);
}

bool StackElement::getAnnotationModification(Packet* p)
{
    return (bool)(p->anno_u8(offsetAnnotation));
}

void StackElement::packetModified(Packet* p, int offset)
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

void StackElement::modifyPacket(Packet* packet, int offset)
{
    // Call the "packetModified" method on every element in the stack
    struct stackElementListNode* current = stackElementList;
    while(current != NULL)
    {
        current->node->packetModified(packet, offset);
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
    uint32_t offset = getContentOffset(p);

    return (p->data() + offset);
}

unsigned char* StackElement::getPacketContent(WritablePacket* p)
{
    uint32_t offset = getContentOffset(p);

    return (p->data() + offset);
}

bool StackElement::isPacketContentEmpty(Packet* packet)
{
    uint32_t offset = getContentOffset(packet);

    if(offset >= packet->length())
        return true;
    else
        return false;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(StackElement)
//ELEMENT_MT_SAFE(StackElement)
