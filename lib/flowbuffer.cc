/*
 * flowbuffer.hh - Class used to buffer the packets of a flow in order to be able to
 * search, replace or remove data in them as in a contiguous flow.
 * This file also defines the iterators used to achieve this goal.
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/flowbuffer.hh>
#include "../elements/flow/stackelement.hh"

CLICK_DECLS

FlowBuffer::FlowBuffer()
{
    /*
     * This class is to be used in FCB, so any field should be assumed 0 and the constructor not to have run
     */
    head = NULL;
}

FlowBuffer::~FlowBuffer()
{
    /**
     * Don't forget to call this operator when the FCB is released
     */
    if (head)
        head->kill();
}



void FlowBuffer::enqueueAll(PacketBatch* batch) {
    if (head != 0) {
        head->append_batch(batch);
    } else {
        head = batch;
    }
    return;
}

void FlowBuffer::enqueue(Packet *packet)
{
    if(unlikely(!isInitialized()))
    {
        click_chatter("Error: FlowBuffer not initialized");
        return;
    }

    // Add the node at the end of the list
    if (head) {
        head->append_packet(packet);
    } else {
        head = PacketBatch::make_from_packet(packet);
    }
}

FlowBufferContentIter FlowBuffer::contentBegin(int posInFirstPacket)
{
    return FlowBufferContentIter(this, head,posInFirstPacket);
}

FlowBufferContentIter FlowBuffer::contentEnd()
{
    return FlowBufferContentIter(this, NULL);
}


int FlowBuffer::searchInFlow(const char *pattern)
{
    if(!isInitialized())
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

Packet* FlowBuffer::dequeue()
{
    if(!isInitialized())
    {
        click_chatter("Error: FlowBuffer not initialized");
        return NULL;
    }

    // Remove the node from the beginning of the list
    if(head == NULL)
        return NULL;

    Packet *packet = head;

    if (head->next() == 0) {
        head = 0;
    } else {
        head = PacketBatch::make_from_simple_list(head->next(), head->tail(), head->count() - 1);
    }
    return packet;
}

PacketBatch* FlowBuffer::dequeueAll()
{
    if(!isInitialized())
    {
        click_chatter("Error: FlowBuffer not initialized");
        return NULL;
    }

    // Remove the node from the beginning of the list
    if(head == NULL)
        return NULL;

    PacketBatch *batch = head;
    head = 0;
    return batch;
}

PacketBatch* FlowBuffer::dequeueUpTo(Packet* packet)
{
    if(!isInitialized())
    {
        click_chatter("Error: FlowBuffer not initialized");
        return NULL;
    }

    // Remove the node from the beginning of the list
    if(head == NULL || packet == head)
        return NULL;

    Packet* next = head ->next();
    Packet* last = head;
    int count = 1;

    while (next != 0 && next != packet) {
        next = next->next();
        last = next;
        count ++;
    }
    PacketBatch* todequeue;
    PacketBatch* second;
    head->cut(last,count,second);
    todequeue = head;
    head = second;

    return todequeue;
}


FlowBufferIter FlowBuffer::begin()
{
    return FlowBufferIter(this, head);
}

FlowBufferIter FlowBuffer::end()
{
    return FlowBufferIter(this, NULL);
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


int FlowBuffer::removeInFlow(const char* pattern, StackElement* owner)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    remove(iter, strlen(pattern), owner);

    return 1;
}

void FlowBuffer::remove(FlowBufferContentIter start, uint32_t length, StackElement* owner)
{
    uint32_t toRemove = length;
    uint32_t offsetInPacket = start.offsetInPacket;
    WritablePacket *packet = static_cast<WritablePacket*>(start.entry);

    // Continue until there are still data to remove
    while(toRemove > 0)
    {
        assert(packet != NULL);

        // Check how much data we have to remove in this packet
        uint32_t inThisPacket = packet->length() - offsetInPacket - StackElement::getContentOffset(packet);

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
        packet = static_cast<WritablePacket*>(packet->next());
    }
}

int FlowBuffer::replaceInFlow(const char* pattern, const char *replacement, StackElement* owner)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    click_chatter("FOUND PATTERN");

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
    WritablePacket *entry = static_cast<WritablePacket*>(iter.entry);

    // When we reach the end of one of the strings, we either have to
    // remove content (if the replacement is shorter) or add content
    // if the replacement if longer
    if(offset > 0)
    {
            // Insert a number of bytes equal to the difference between the lengths of
            // the replacement and the pattern
            entry = owner->insertBytes(entry, offsetInPacket, offset);

            // Copy the rest of the replacement where we added bytes
            for(int i = 0; i < lenReplacement - toReplace; ++i)
                owner->getPacketContent(entry)[offsetInPacket + i] = replacement[toReplace + i];

            return 1;
    }
    else
    {
            // We remove the next "-offset" (offset is negative) bytes in the flow
            remove(iter, -offset, owner);
    }

    return 1;
}



// FlowBuffer Iterator
FlowBufferIter::FlowBufferIter(FlowBuffer *_flowBuffer,
    Packet* _entry) : flowBuffer(_flowBuffer)
{
    entry = static_cast<WritablePacket*>(_entry);
}

WritablePacket*& FlowBufferIter::operator*()
{
    assert(entry != NULL);

    return entry;
}

FlowBufferIter& FlowBufferIter::operator++()
{
    assert(entry != NULL);

    entry = static_cast<WritablePacket*>(entry->next());

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
    Packet* _entry, int posInFirstPacket) : flowBuffer(_flowBuffer), entry(_entry), offsetInPacket(posInFirstPacket)
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

    unsigned char* content = StackElement::getPacketContent(static_cast<WritablePacket*>(entry));

    return *(content + offsetInPacket);
}

void FlowBufferContentIter::repair()
{
    // This method must be called after a deletion at the end of a packet

    assert(entry != NULL);

    //Only writablepacket must be enqueued if in write mode
    WritablePacket* wPacket = static_cast<WritablePacket*>(entry);

    // Check if we are pointing after the content of the current packet
    if(offsetInPacket >= StackElement::getPacketContentSize(wPacket))
    {
        // If so, we move to the next packet
        uint16_t contentSize = StackElement::getPacketContentSize(wPacket);
        uint16_t overflow = offsetInPacket - contentSize;
        offsetInPacket = overflow;
        entry = entry->next();

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
    if(StackElement::getContentOffset(entry) + offsetInPacket >=
        entry->length())
    {
        offsetInPacket = 0;
        entry = entry->next();
    }

    return *this;
}

PacketBatch* FlowBufferContentIter::flush() {
    click_chatter("Flush %p",entry);
    if (entry == 0) {
        return this->flowBuffer->dequeueAll();
    } else {

        return this->flowBuffer->dequeueUpTo(entry);
    }

}

CLICK_ENDDECLS

ELEMENT_PROVIDES(FlowBuffer)
