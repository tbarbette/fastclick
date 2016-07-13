#include <click/config.h>
#include <click/glue.hh>
#include "fcb.hh"
#include "flowbuffer.hh"
#include "stackelement.hh"

CLICK_DECLS

FlowBuffer::FlowBuffer()
{
    poolEntries = NULL;
    head = NULL;
    tail = NULL;
    owner = NULL;
    initialized = false;
}

FlowBuffer::~FlowBuffer()
{
    if(!initialized)
        return;

    struct flowBufferEntry *current = head;

    while(current != NULL)
    {
        struct flowBufferEntry* toRemove = current;

        current = current->next;

        poolEntries->releaseMemory(toRemove);
    }
}

MemoryPool<struct flowBufferEntry>* FlowBuffer::getMemoryPool()
{
    return poolEntries;
}

bool FlowBuffer::isInitialized()
{
    return initialized;
}

void FlowBuffer::enqueue(WritablePacket *packet)
{
    if(!initialized)
    {
        click_chatter("Error: FlowBuffer not initialized");
        return;
    }

    struct flowBufferEntry *entry = poolEntries->getMemory();

    entry->packet = packet;
    entry->prev = tail;
    entry->next = NULL;

    if(tail != NULL)
        tail->next = entry;

    tail = entry;

    if(head == NULL)
        head = entry;
}

FlowBufferContentIter FlowBuffer::contentBegin()
{
    return FlowBufferContentIter(this, head);
}

FlowBufferContentIter FlowBuffer::contentEnd()
{
    return FlowBufferContentIter(this, NULL);
}

int FlowBuffer::searchInFlow(const char *pattern)
{
    if(!initialized)
    {
        click_chatter("Error: FlowBuffer not initialized");
        return -1;
    }

    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    return 1;
}

WritablePacket* FlowBuffer::dequeue()
{
    if(!initialized)
    {
        click_chatter("Error: FlowBuffer not initialized");
        return NULL;
    }

    if(head == NULL)
        return NULL;

    WritablePacket *packet = head->packet;

    if(head == tail)
        tail = NULL;

    struct flowBufferEntry *toRemove = head;

    head = head->next;

    if(head != NULL)
        head->prev = NULL;

    poolEntries->releaseMemory(toRemove);

    return packet;
}

FlowBufferIter FlowBuffer::begin()
{
    return FlowBufferIter(this, head);
}

FlowBufferIter FlowBuffer::end()
{
    return FlowBufferIter(this, NULL);
}

void FlowBuffer::initialize(StackElement *owner, MemoryPool<struct flowBufferEntry> *poolEntries)
{
    this->poolEntries = poolEntries;
    this->owner = owner;
    initialized = true;
}

StackElement* FlowBuffer::getOwner()
{
    return owner;
}

FlowBufferContentIter FlowBuffer::search(FlowBufferContentIter start, const char* pattern, int *feedback)
{
    const char *currentPattern = pattern;
    while(start != contentEnd())
    {
        currentPattern = pattern;
        FlowBufferContentIter currentContent = start;

        while(currentContent != contentEnd() && (tolower(*currentContent) == tolower(*currentPattern)) && *currentPattern != '\0')
        {
            ++currentPattern;
            ++currentContent;
        }

        if(*currentPattern == '\0')
        {
            *feedback = 1;
            return start;
        }

        ++start;
    }

    // If we have found at least one matching character at the end, tell it
    // as the rest may be in the next packet
    if(*currentPattern != *pattern)
        *feedback = 0;
    else
        *feedback = -1;

    return contentEnd();
}

bool FlowBuffer::removeInFlow(struct fcb *fcb, const char* pattern)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return false;

    remove(fcb, iter, strlen(pattern));

    return true;
}

void FlowBuffer::remove(struct fcb *fcb, FlowBufferContentIter start, uint32_t length)
{
    uint32_t toRemove = length;
    uint32_t offsetInPacket = start.offsetInPacket;
    struct flowBufferEntry* entry = start.entry;
    WritablePacket *packet = entry->packet;

    while(toRemove > 0)
    {
        assert(entry != NULL);

        uint32_t inThisPacket = packet->length() - offsetInPacket - owner->getContentOffset(packet);

        if(inThisPacket > toRemove)
        {
            inThisPacket = toRemove;
            toRemove = 0;
        }
        else
            toRemove -= inThisPacket;

        owner->removeBytes(fcb, packet, offsetInPacket, inThisPacket);

        offsetInPacket = 0;
        entry = entry->next;

        if(entry != NULL)
            packet = entry->packet;
    }
}

bool FlowBuffer::replaceInFlow(struct fcb *fcb, const char* pattern, const char *replacement)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return false;

    uint32_t lenPattern = strlen(pattern);
    uint32_t lenReplacement = strlen(replacement);

    long offset = lenReplacement - lenPattern;

    uint32_t toReplace = lenPattern;
    if(toReplace > lenReplacement)
        toReplace = lenReplacement;

    // Replace pattern by replacement until we reach the end of one of
    // the two strings
    for(int i = 0; i < toReplace; ++i)
    {
        *iter = replacement[i];
        ++iter;
    }

    uint32_t offsetInPacket = iter.offsetInPacket;
    struct flowBufferEntry* entry = iter.entry;
    WritablePacket *packet = entry->packet;

    // When we reach the end of one of the strings, we either have to
    // remove content (if the replacement is shorter) or add content
    // if the replacement if longer
    if(offset > 0)
    {
        // Insert a number of bytes equal to the difference between
        // the replacement and the pattern
        packet = owner->insertBytes(fcb, packet, offsetInPacket, offset);
        entry->packet = packet;

        // Copy the rest of the replacement where we added bytes
        for(int i = 0; i < lenReplacement - toReplace; ++i)
            owner->getPacketContent(packet)[offsetInPacket + i] = replacement[toReplace + i];

        return true;
    }
    else
    {
        // We remove the next "-offset" (offset is negative) bytes in the flow
        remove(fcb, iter, -offset);
    }

    return true;
}



// FlowBuffer Iterator
FlowBufferIter::FlowBufferIter(FlowBuffer *_flowBuffer, flowBufferEntry* _entry) : flowBuffer(_flowBuffer), entry(_entry)
{

}

WritablePacket*& FlowBufferIter::operator*()
{
    assert(entry != NULL);

    return entry->packet;
}

FlowBufferIter& FlowBufferIter::operator++()
{
    assert(entry != NULL);

    entry = entry->next;
}


bool FlowBufferIter::operator==(const FlowBufferIter& other) const
{
    if(this->flowBuffer != other.flowBuffer)
        return false;

    if(this->entry == NULL && other.entry == NULL)
        return true;

    if(this->entry != other.entry)
        return false;

    return true;
}

bool FlowBufferIter::operator!=(const FlowBufferIter& other) const
{
    return !(*this == other);
}

// FlowBufferContent Iterator
FlowBufferContentIter::FlowBufferContentIter(FlowBuffer *_flowBuffer, flowBufferEntry* _entry) : flowBuffer(_flowBuffer), entry(_entry), offsetInPacket(0)
{

}

bool FlowBufferContentIter::operator==(const FlowBufferContentIter& other) const
{
    if(this->flowBuffer != other.flowBuffer)
        return false;

    if(this->entry == NULL && other.entry == NULL)
        return true;

    if(this->entry != other.entry)
        return false;

    if(this->offsetInPacket != other.offsetInPacket)
        return false;

    return true;
}

bool FlowBufferContentIter::operator!=(const FlowBufferContentIter& other) const
{
    return !(*this == other);
}

unsigned char& FlowBufferContentIter::operator*()
{
    assert(entry != NULL);

    unsigned char* content = flowBuffer->getOwner()->getPacketContent(entry->packet);

    return *(content + offsetInPacket);
}

FlowBufferContentIter& FlowBufferContentIter::operator++()
{
    assert(entry != NULL);

    offsetInPacket++;

    // Check if we are at the end of the packet and must therefore switch to
    // the next one
    if(flowBuffer->getOwner()->getContentOffset(entry->packet) + offsetInPacket >= entry->packet->length())
    {
        offsetInPacket = 0;
        entry = entry->next;
    }
}


CLICK_ENDDECLS

ELEMENT_PROVIDES(FlowBuffer)
