/*
 * flowbuffer.hh - Class used to buffer the packets of a flow in order to be able to
 * search, replace or remove data in them as in a contiguous flow.
 * This file also defines the iterators used to achieve this goal.
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
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
    size = 0;
}

FlowBuffer::~FlowBuffer()
{
    if(!initialized)
        return;

    struct flowBufferEntry *current = head;

    // Put back the nodes in the memory pool
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

    // Get memory from the pool in order to store a node
    struct flowBufferEntry *entry = poolEntries->getMemory();

    // Add the node at the end of the list
    entry->packet = packet;
    entry->prev = tail;
    entry->next = NULL;

    if(tail != NULL)
        tail->next = entry;

    tail = entry;

    if(head == NULL)
        head = entry;

    size++;
}

FlowBufferContentIter FlowBuffer::contentBegin()
{
    return FlowBufferContentIter(this, head);
}

FlowBufferContentIter FlowBuffer::contentEnd()
{
    return FlowBufferContentIter(this, NULL);
}

uint32_t FlowBuffer::getSize()
{
    return size;
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

    // Remove the node from the beginning of the list
    if(head == NULL)
        return NULL;

    WritablePacket *packet = head->packet;

    if(head == tail)
        tail = NULL;

    struct flowBufferEntry *toRemove = head;

    head = head->next;

    if(head != NULL)
        head->prev = NULL;

    // Put back the memory in the pool
    poolEntries->releaseMemory(toRemove);

    size--;

    // Return the packet
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

FlowBufferContentIter FlowBuffer::search(FlowBufferContentIter start, const char* pattern,
    int *feedback)
{
    const char *currentPattern = pattern;
    int nbFound = 0;

    // Search until we reach the end of the buffer
    while(start != contentEnd())
    {
        currentPattern = pattern;
        FlowBufferContentIter currentContent = start;
        nbFound = 0;

        // Check if the characters in the buffer and in the pattern match (case insensitive)
        while(currentContent != contentEnd()
            && (tolower(*currentContent) == tolower(*currentPattern)) && *currentPattern != '\0')
        {
            ++currentPattern;
            ++currentContent;
            nbFound++;
        }

        // We have reached the end of the pattern, it means that we found it
        if(*currentPattern == '\0')
        {
            *feedback = 1;
            return start;
        }

        // If we have found at least one matching character at the end, the rest may be in the
        // next packet
        if(currentContent == contentEnd() && nbFound > 0)
        {
            *feedback = 0;
            return contentEnd();
        }

        ++start;
    }

    // Nothing found
    *feedback = -1;

    return contentEnd();
}

int FlowBuffer::removeInFlow(const char* pattern)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    remove(iter, strlen(pattern));

    return 1;
}

void FlowBuffer::remove(FlowBufferContentIter start, uint32_t length)
{
    uint32_t toRemove = length;
    uint32_t offsetInPacket = start.offsetInPacket;
    struct flowBufferEntry* entry = start.entry;
    WritablePacket *packet = entry->packet;

    // Continue until there are still data to remove
    while(toRemove > 0)
    {
        assert(entry != NULL);

        // Check how much data we have to remove in this packet
        uint32_t inThisPacket = packet->length() - offsetInPacket - owner->getContentOffset(packet);

        // Update the counter of remaining data to remove
        if(inThisPacket > toRemove)
        {
            inThisPacket = toRemove;
            toRemove = 0;
        }
        else
            toRemove -= inThisPacket;

        // Remove the data in this packet
        owner->removeBytes(packet, offsetInPacket, inThisPacket);

        // Go to the next packet
        offsetInPacket = 0;
        entry = entry->next;

        if(entry != NULL)
            packet = entry->packet;
    }
}

int FlowBuffer::replaceInFlow(const char* pattern, const char *replacement)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    uint32_t lenPattern = strlen(pattern);
    uint32_t lenReplacement = strlen(replacement);

    // We compute the difference between the size of the previous content and the new one
    long offset = lenReplacement - lenPattern;

    // We compute how many bytes of the old content will be replaced by the new content
    uint32_t toReplace = lenPattern;
    if(toReplace > lenReplacement)
        toReplace = lenReplacement;

    // Replace pattern by "replacement" until we reach the end of one of the two strings
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
        // Insert a number of bytes equal to the difference between the lengths of
        // the replacement and the pattern
        packet = owner->insertBytes(packet, offsetInPacket, offset);
        entry->packet = packet;

        // Copy the rest of the replacement where we added bytes
        for(int i = 0; i < lenReplacement - toReplace; ++i)
            owner->getPacketContent(packet)[offsetInPacket + i] = replacement[toReplace + i];

        return 1;
    }
    else
    {
        // We remove the next "-offset" (offset is negative) bytes in the flow
        remove(iter, -offset);
    }

    return 1;
}



// FlowBuffer Iterator
FlowBufferIter::FlowBufferIter(FlowBuffer *_flowBuffer,
    flowBufferEntry* _entry) : flowBuffer(_flowBuffer), entry(_entry)
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

    return *this;
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
FlowBufferContentIter::FlowBufferContentIter(FlowBuffer *_flowBuffer,
    flowBufferEntry* _entry) : flowBuffer(_flowBuffer), entry(_entry), offsetInPacket(0)
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

void FlowBufferContentIter::repair()
{
    // This method must be called after a deletion at the end of a packet

    assert(entry != NULL);

    // Check if we are pointing after the content of the current packet
    if(offsetInPacket >= flowBuffer->getOwner()->getPacketContentSize(entry->packet))
    {
        // If so, we move to the next packet
        uint16_t contentSize = flowBuffer->getOwner()->getPacketContentSize(entry->packet);
        uint16_t overflow = offsetInPacket - contentSize;
        offsetInPacket = overflow;
        entry = entry->next;

        // Check if we need to continue for the next packet
        // It will be executed recursively until the iterator is repaired or
        // we reached the end of the flow
        if(entry != NULL)
            repair();
        else
            offsetInPacket = 0;
    }
}

FlowBufferContentIter& FlowBufferContentIter::operator++()
{
    assert(entry != NULL);

    offsetInPacket++;

    // Check if we are at the end of the packet and must therefore switch to
    // the next one
    if(flowBuffer->getOwner()->getContentOffset(entry->packet) + offsetInPacket >=
        entry->packet->length())
    {
        offsetInPacket = 0;
        entry = entry->next;
    }

    return *this;
}

CLICK_ENDDECLS

ELEMENT_PROVIDES(FlowBuffer)
