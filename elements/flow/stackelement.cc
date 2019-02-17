#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/packet_anno.hh>
#include "stackelement.hh"


CLICK_DECLS

bool StackVisitor::visit(Element *e, bool, int port, Element*, int, int)
{
    // Check that the element is a stack element
    // If this is not the case, we skip it and continue the traversing
    if(!StackElement::isStackElement(e)) {
        //click_chatter("%p{element} is not stack",e);
        return true;
    }

    // We now know that we have a stack element so we can cast it
    StackElement *element = reinterpret_cast<StackElement*>(e);

    // Add the starting element in the list of the current element
    click_chatter("Adding element %p{element} as predecessor of %p{element}",
            startElement, element);
    element->addStackElementInList(startElement, port);

    // Stop search when we encounter the IPOut Element
    if(strcmp(element->class_name(), "IPOut") == 0)
        return false;

    // Stop the traversing
    return false;
}

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
        return VirtualFlowSpaceElement::cast(name);
 }

void StackElement::buildFunctionStack()
{
    StackVisitor visitor(this);
    this->router()->visit_downstream(this, -1, &visitor);
}


int StackElement::event(ErrorHandler*  errh, EventType phase)
{
    int e = BatchElement::event(errh, phase);
    if (e != 0)
        return e;

    switch (phase) {
        case INIT_PLATFORM:
                buildFunctionStack();
            return 0;
    }

    return e;
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


char* StackElement::searchInContent(char *content, const char *pattern, uint32_t length) {
    // We use this method instead of a mere 'strstr' because the content of the packet
    // is not necessarily NULL-terminated

    uint32_t patternLen = strlen(pattern);

    for(uint32_t i = 0; i < length; ++i)
    {
        if(patternLen + i > length)
            return NULL;

        if(strncmp(&content[i], pattern, patternLen) == 0)
            return &content[i];
    }

    return NULL;
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

bool StackElement::isEstablished()
{
    // Call the "closeConnection" method on every element in the stack
    if(previousStackElement == NULL) {
        return false;
    }

    return previousStackElement->isEstablished();
}


bool StackElement::registerConnectionClose(StackReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt, void* thunk)
{
    // Call the "closeConnection" method on every element in the stack
    if(previousStackElement == NULL) {
        click_chatter("No previous stack in %p{element}", this);
        return false;
    }

    return previousStackElement->registerConnectionClose(fcb_chain, fnt, thunk);
}

int StackElement::maxModificationLevel(Element* stop) {
    assert(router()->handlers_ready());
    if(previousStackElement == stop || previousStackElement == 0)
        return 0;

    return previousStackElement->maxModificationLevel(stop);
}

void StackElement::removeBytes(WritablePacket* packet, uint32_t position,
    uint32_t length)
{
    //click_chatter("Calling rmbyte on %p{element}", this);
    // Call the "removeBytes" method on every element in the stack
    if(previousStackElement == NULL) {
        //click_chatter("No previous elem before %p{element}. Removing data",this);
        unsigned char *source = packet->data();
        position += packet->getContentOffset();
        uint32_t bytesAfter = packet->length() - position;
        if (bytesAfter > 0) {
            memmove(&source[position], &source[position + length], bytesAfter);
        }
        packet->take(length);
        return;
    }// else
        //click_chatter("Previous elem before %p{element} that is %p{element}",this,previousStackElement);
    previousStackElement->removeBytes(packet, position, length);
}

WritablePacket* StackElement::insertBytes(WritablePacket* packet,
    uint32_t position, uint32_t length)
{
    // Call the "insertBytes" method on every element in the stack
    if(previousStackElement == NULL) {
        uint32_t bytesAfter = packet->length() - position;
//        click_chatter("bytes after %d, position %d, put %d", bytesAfter, position, length);
        WritablePacket *newPacket = packet->put(length);
        assert(newPacket != NULL);
        unsigned char *source = newPacket->data();
        if (bytesAfter > 0)
            memmove(&source[position + length], &source[position], bytesAfter);
        return newPacket;
    }

    return previousStackElement->insertBytes(packet, position, length);
}

void StackElement::requestMorePackets(Packet *packet, bool force)
{
    //click_chatter("%p{element} : requestMorePackets", previousStackElement);

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
