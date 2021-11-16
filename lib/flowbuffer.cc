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
#include <click/flow/ctxelement.hh>
#include <immintrin.h>

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

FlowBufferContentIter FlowBuffer::enqueueAllIter(PacketBatch* batch) {
    if (head != 0) {
        head->append_batch(batch);
    } else {
        head = batch;
    }
    return FlowBufferContentIter(this,batch->first(),0);
}


FlowBufferChunkIter FlowBuffer::enqueueAllChunkIter(PacketBatch* batch) {
    if (head != 0) {
        head->append_batch(batch);
    } else {
        head = batch;
    }
    return FlowBufferChunkIter(this,batch->first());
}


void FlowBuffer::enqueue(Packet *packet)
{
    // Add the node at the end of the list
    if (head) {
        head->append_packet(packet);
        packet->set_next(0);
    } else {
        head = PacketBatch::make_from_packet(packet);
    }
}

FlowBufferContentIter FlowBuffer::contentBegin(int posInFirstPacket)
{
    return FlowBufferContentIter(this, head->first(), posInFirstPacket);
}

FlowBufferContentIter FlowBuffer::contentEnd()
{
    return FlowBufferContentIter(this, NULL);
}


int FlowBuffer::searchInFlow(const char *pattern)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    return 1;
}

Packet* FlowBuffer::dequeue()
{
    // Remove the node from the beginning of the list
    if(head == NULL)
        return NULL;

    Packet *packet = head->first();

    if (head->first()->next() == 0) {
        head = 0;
    } else {
        head = PacketBatch::make_from_simple_list(head->first()->next(), head->tail(), head->count() - 1);
    }
    return packet;
}

PacketBatch* FlowBuffer::dequeueAll()
{
    // Remove the node from the beginning of the list
    if(head == NULL)
        return NULL;

    PacketBatch *batch = head;
    head = 0;
    return batch;
}

int FlowBuffer::getSize()
{
    return head->count();
}


PacketBatch* FlowBuffer::dequeueUpTo(Packet* packet)
{
    if(!isInitialized())
    {
        click_chatter("Error: FlowBuffer not initialized");
        return NULL;
    }

    // Remove the node from the beginning of the list
    if(head == NULL || packet == head->first())
        return NULL;

    Packet* next = head->first()->next();
    Packet* last = head->first();
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
    return FlowBufferIter(this, head->first());
}

FlowBufferIter FlowBuffer::end()
{
    return FlowBufferIter(this, NULL);
}

FlowBufferContentIter FlowBuffer::searchSSE(FlowBufferContentIter start, const char* needle, const int pattern_length, int *feedback) {
#if HAVE_AVX2
    int n = start.leftInChunk();
    //If the first chunk is small, don't bother SSE
    if (n < 32)
        return search(start,needle,feedback);

    const __m256i first = _mm256_set1_epi8(needle[0]);
    const __m256i last  = _mm256_set1_epi8(needle[pattern_length - 1]);
    unsigned char* s = start.get_ptr();
    int i = 0;
    *feedback = -1;
    FlowBufferContentIter next = start;
    next.moveToNextChunk();
    __m256i eq_first;

    while (true) {
        const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        const __m256i block_last  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + pattern_length - 1));

        eq_first = _mm256_cmpeq_epi8(first, block_first);
        const __m256i eq_last  = _mm256_cmpeq_epi8(last, block_last);

        uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

        while (mask != 0) {

            const auto bitpos = __builtin_ctz(mask);

            if (memcmp(s + i + bitpos + 1, needle + 1, pattern_length - 2) == 0) {
                start += (i + bitpos);
                *feedback = 1;
                return start;
            }

            mask = mask & (mask - 1);
        }
        i += 32;
        if (i + 32 + pattern_length >= n) {
            if (start.lastChunk() || next == 0) {
                //We have to check if a pattern could start in the last part
                uint32_t z = _mm256_movemask_epi8(eq_first);
                if (z != 0) {
                    start += min(n - pattern_length, i - pattern_length);
                } else {
                    start += i - pattern_length;
                }

                //click_chatter("searching now!");
                return search(start,needle,feedback);
            } else {
                if (i + pattern_length >= n) {
                    //click_chatter("Next chunk");
                    start = next;
                    //int am = i+pattern_length-n;
                    i = 0;
                    s = start.get_ptr();
                    n = start.leftInChunk();
                } else {
                    //click_chatter("InterChunk prep!");
                    int am = i+32+pattern_length-n;
                    next = start;
                    next.moveToNextChunk();
                    //click_chatter("Copy %s at the end of %s, %d",(char*)next.get_ptr(),(char*)s+n,am);
                    strncpy((char*)s+n,(char*)next.get_ptr(),am);
                    //click_chatter("Result (am is %d): %s",am,s);
                    next += am;
                }
            }
        }
    }
    //Should be unreachable
    assert(false);

    *feedback = -1;
    return contentEnd();
#else
    return search(start,needle,feedback);
#endif
}

FlowBufferContentIter FlowBuffer::isearch(FlowBufferContentIter start, const char* pattern,
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

        // Check if the characters in the buffer and in the pattern match (case sensitive)
        while(currentContent != contentEnd()
            && (*currentContent == *currentPattern) && *currentPattern != '\0')
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
            return start;
        }

        ++start;
    }

    // Nothing found
    *feedback = -1;

    return contentEnd();
}


int FlowBuffer::removeInFlow(const char* pattern,const int pattern_length, CTXElement* owner)
{
    int feedback = -1;
    FlowBufferContentIter iter = search(contentBegin(), pattern, &feedback);

    if(iter == contentEnd())
        return feedback;

    remove(iter, pattern_length, owner);

    return 1;
}

void FlowBuffer::remove(const FlowBufferContentIter start, uint32_t length, CTXElement* owner)
{
    assert(length > 0);
    uint32_t toRemove = length;
    uint32_t offsetInPacket = start.offsetInPacket;
    WritablePacket *packet = static_cast<WritablePacket*>(start.entry);

    // Continue while the is data to remove
    while(toRemove > 0)
    {
        assert(packet != NULL);

        // Check how much data we have to remove in this packet
        uint32_t inThisPacket = packet->length() - offsetInPacket - packet->getContentOffset();

        // Update the counter of remaining data to remove
        if(inThisPacket > toRemove)
        {
            inThisPacket = toRemove;
            toRemove = 0;
        }
        else
            toRemove -= inThisPacket;

        //click_chatter("To remove : %d. In this packet %d",toRemove, inThisPacket);

        // Remove the data in this packet
        owner->removeBytes(packet, offsetInPacket, inThisPacket);

        // Go to the next packet
        offsetInPacket = 0;
        packet = static_cast<WritablePacket*>(packet->next());
    }
}

int FlowBuffer::replaceInFlow(FlowBufferContentIter iter, const int pattern_length, const char *replacement, const int replacement_length, const bool repeat, CTXElement* owner)
{
    int feedback = -1;
    if(iter == contentEnd())
        return feedback;

    // We compute how many bytes of the old content will be replaced by the new content

    long firstReplace;
    uint32_t toReplace;
    int32_t offset;
    if (repeat) {
        toReplace = pattern_length;
        firstReplace = min(pattern_length, (int)toReplace);
        offset = 0;
    } else { //Do not repeast replacement
        if (pattern_length > replacement_length) { // We remove some bytes
            toReplace = pattern_length;
            firstReplace = replacement_length;
            offset = replacement_length - pattern_length;
        } else { //We add some bytes
            toReplace = replacement_length;
            firstReplace = pattern_length;
            offset = toReplace - firstReplace;
        }
    }


    // Replace pattern by "replacement" until we reach the end of one of the two strings
    for(int i = 0; i < firstReplace; ++i)
    {
        *iter = replacement[i % replacement_length];
        ++iter;
    }

    uint32_t offsetInPacket = iter.offsetInPacket;
    WritablePacket* entry = static_cast<WritablePacket*>(iter.entry);

    // When we reach the end of one of the strings, we either have to
    // remove content (if the replacement is shorter) or add content
    // if the replacement if longer
    if(offset > 0)
    {
            // Insert a number of bytes equal to the difference between the lengths of
            // the replacement and the pattern
            //click_chatter("Insert at %d, offset %d", offsetInPacket, offset);
            entry = owner->insertBytes(entry, offsetInPacket, offset);

            assert(entry);

            // Copy the rest of the replacement where we added bytes
            for(int i = 0; i < toReplace - firstReplace; ++i) {
                entry->getPacketContent()[offsetInPacket + i] = replacement[firstReplace + i];
            }
    }
    else if (offset < 0)
    {
            // We remove the next "-offset" (offset is negative) bytes in the flow
            //click_chatter("Remove %d bytes at %d", -offset, offsetInPacket);
            remove(iter, -offset, owner);
    }

    return max((int)toReplace,pattern_length);
}

void FlowBufferContentIter::repair()
{
    // This method must be called after a deletion at the end of a packet

    assert(entry != NULL);

    //Only writablepacket must be enqueued if in write mode
    WritablePacket* wPacket = static_cast<WritablePacket*>(entry);

    // Check if we are pointing after the content of the current packet
    if(offsetInPacket >= wPacket->getPacketContentSize())
    {
        // If so, we move to the next packet
        uint16_t contentSize = wPacket->getPacketContentSize();
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

CLICK_ENDDECLS

ELEMENT_PROVIDES(FlowBuffer)
