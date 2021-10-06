/*
 * flowbuffer.hh - Class used to buffer the packets of a flow in order to be able to
 * search, replace or remove data in them as in a contiguous flow.
 * This file also declares the iterators used to achieve this goal.
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_FLOWBUFFER_HH
#define MIDDLEBOX_FLOWBUFFER_HH

#include <clicknet/tcp.h>
#include "memorypool.hh"
#include <click/packet.hh>

CLICK_DECLS

class CTXElement;

struct flowBufferEntry
{
        WritablePacket *packet;
        struct flowBufferEntry *prev;
        struct flowBufferEntry *next;
};

class FlowBufferContentIter;
class FlowBufferChunkIter;
class FlowBufferIter;
class CTXElement;
struct fcb;

/** @class FlowBuffer
 * @brief This buffer allows to search, replace or remove data in the packets buffered
 * as in a contiguous flow. It also allows to determine if a pattern could be starting at the
 * end of the last packet in the buffer and thus if buffering more packets is required to
 * make a decision.
 */
class FlowBuffer
{
public:
    friend class FlowBufferIter;

    /** @brief Construct a FlowBuffer
     * The FlowBuffer must be initialized
     */
    FlowBuffer();

    /** @brief Destruct a FlowBuffer
     */
    ~FlowBuffer();

    /** @brief Return a pointer to the MemoryPool used by this FlowBuffer
     * @return A pointer to the MemoryPool used by this FlowBuffer
     */
    MemoryPool<struct flowBufferEntry>* getMemoryPool();

    /** @brief Enqueue a packet at the end of the buffer
     * @param packet The packet to enqueue
     */
    void enqueue(Packet *packet);

    /** @brief Dequeue a packet from the beginning of the buffer
     * @returnt The dequeued packet
     */
    Packet* dequeue();

    /** @brief Dequeue all packets
     * @returnt The dequeued packet
     */
    PacketBatch* dequeueAll();

    /** @brief Dequeue packet up to the given one, not included
     * @returnt The dequeued packet
     */
    PacketBatch* dequeueUpTo(Packet*);

    /** @brief Return the size of the buffer
     * @return The number of packets in the buffer
     */
    int getSize();

    /** @brief Return an iterator pointing to the first packet of the buffer
     * @return An iterator pointing to the first packet of the buffer
     */
    FlowBufferIter begin();

    /** @brief Return an iterator pointing to the end of the buffer
     * @return An iterator pointing to the end of the buffer
     */
    FlowBufferIter end();

    /** @brief Search a pattern in the buffer
     * @param pattern The pattern to search
     * @return 1 if the pattern has been found, -1 if the pattern has not been found
     * and cannot start in the last packet of the buffer, 0 if the pattern has not been found
     * but the pattern could start at the end of the last packet in the buffer and thus enqueuing
     * the next packet could result in a match.
     */
    int searchInFlow(const char *pattern);

    /** @brief Remove a pattern in the buffer
     * @param fcb A pointer to the FCB of this flow
     * @param The pattern to remove
     * @return 1 if the pattern has been found and the first occurrence has been removed
     * -1 if the pattern has not been found and cannot start in the last packet of the buffer
     * 0 if the pattern has not been found but the pattern could start at the end of the last
     * packet in the buffer and thus enqueuing the next packet could result in a match.
     */
    int removeInFlow(const char* pattern, const int pattern_length, CTXElement* remove);

    /** @brief Replace a pattern in the buffer
     * @param fcb A pointer to the FCB of this flow
     * @param The pattern to replace
     * @param The new data that will replace the old one
     * @return 1 if the pattern has been found and the first occurrence has been replaced
     * -1 if the pattern has not been found and cannot start in the last packet of the buffer
     * 0 if the pattern has not been found but the pattern could start at the end of the last
     * packet in the buffer and thus enqueuing the next packet could result in a match.
     */
    int replaceInFlow(FlowBufferContentIter pos, const int pattern_length, const char *replacement, const int replacement_length, const bool repeat, CTXElement* owner);

    /** @brief Return a content iterator pointing to the first byte of content in the buffer
     * @return A content iterator pointing to the first byte of content in the buffer
     */
    FlowBufferContentIter contentBegin(int posInFirstPacket = 0);

    /** @brief Return a content iterator pointing after the end of the content in the buffer
     * @return A content iterator pointing after the end of the content in the buffer
     */
    FlowBufferContentIter contentEnd();


    /**
     * @brief Ensure a flow buffer is initialized and enqueue all packets
     */
    void enqueueAll(PacketBatch* batch);


    FlowBufferContentIter enqueueAllIter(PacketBatch* batch);
    FlowBufferChunkIter enqueueAllChunkIter(PacketBatch* batch);

    /** @brief Search a pattern in the buffer
     * @param start Content iterator indicating where to start the search
     * @param pattern The pattern to search
     * @param feedback An int in which the result of the search will be put (1 if found, -1 if
     * not found, 0 if not found but could start at the end of the last packet)
     * @return An iterator pointing to the beginning of the pattern in the flow if found
     * or after the end of the content if not found
     */
    FlowBufferContentIter search(FlowBufferContentIter start, const char* pattern, int *feedback);
    FlowBufferContentIter isearch(FlowBufferContentIter start, const char* pattern, int *feedback);
    FlowBufferContentIter searchSSE(FlowBufferContentIter start, const char* pattern, const int pattern_length, int *feedback);

    /** @brief Remove data in the flow (across the packets)
     * @param fcb A pointer to the FCB of the flow
     * @param start A content iterator pointing to the first byte to remove
     * @param length The number of bytes to remove
     */
    void remove(const FlowBufferContentIter start, uint32_t length, CTXElement* owner);


private:
    inline bool isInitialized() {
        return head != 0;
    }



    PacketBatch *head;
};

/** @class FlowBufferIter
 * @brief This iterator allows to iterate over the packets in a FlowBuffer
 */
class FlowBufferIter
{
public:
    /** @brief Construct a FlowBufferIter
     * @param _flowBuffer The FlowBuffer to which this iterator is linked
     * @param _entry The entry in the buffer to which this iterator points
     */
    inline FlowBufferIter(FlowBuffer *_flowBuffer, Packet* _entry);

    /** @brief Compare two FlowBufferIter
     * @param other The FlowBufferIter to be compared to
     * @return True if the two iterators point to the same packet
     */
    inline bool operator==(const FlowBufferIter& other) const;

    /** @brief Compare two FlowBufferIter
     * @param other The FlowBufferIter to be compared to
     * @return False if the two iterators point to the same packet
     */
    inline bool operator!=(const FlowBufferIter& other) const;

    /** @brief Return the packet to which this iterator points
     * @return The packet to which this iterator points
     */
    inline WritablePacket*& operator*();

    /** @brief Move the iterator to the next packet in the buffer
     * @return The iterator moved
     */
    inline FlowBufferIter& operator++();

private:
    FlowBuffer *flowBuffer;
    WritablePacket* entry; // Current entry
};

/** @class FlowBufferContentIter
 * @brief This iterator allows to iterate the content in the buffer seamlessly accross the packets
 */
class FlowBufferContentIter
{
public:
    friend class FlowBuffer;

    inline FlowBufferContentIter() {}; //Invalid placeholder

    /** @brief Construct a FlowBufferContentIter
     * @param _flowBuffer The FlowBuffer to which this iterator is linked
     * @param _entry The entry in the buffer to which this iterator points
     */
    inline FlowBufferContentIter(FlowBuffer *_flowBuffer, Packet* packet, int posInFirstPacket=0);

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return True if the two iterators point to the same data in the flow
     */
    inline bool operator==(const FlowBufferContentIter& other) const;

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return False if the two iterators point to the same data in the flow
     */
    inline bool operator!=(const FlowBufferContentIter& other) const;

    /** @brief Return the byte to which this iterator points
     * @return The byte to which this iterator points
     */
    inline unsigned char& operator*();

    /** @brief Return the byte to which this iterator points
     * @return The byte to which this iterator points
     */
    inline unsigned char* get_ptr();

    /** @brief Move the iterator to the next byte in the buffer
     * @return The iterator moved
     */
    inline FlowBufferContentIter& operator++();

    /** @brief Move the iterator to the next N bytes in the buffer
     * @return The iterator moved
     */
    inline FlowBufferContentIter& operator+=(int p);

    inline operator bool() const {
        return entry != 0;
    }

    /**
     * Return all packets up to the current position, not included
     */
    inline PacketBatch* flush();

    inline Packet* current() {
        return entry;
    }

    inline bool lastChunk() {
        return entry->next() == 0;
    }

    inline void moveToNextChunk() {
        entry = entry->next();
        offsetInPacket = 0;
    }

    inline int leftInChunk() {
        if (!entry)
            return 0;
        return entry->length() - (entry->getContentOffset() + offsetInPacket);
    }

    void print_ascii() {
        FlowBufferContentIter n = *this;
        while (n) {
            char buf[n.leftInChunk() + 1];
            memcpy(buf, n.get_ptr(), n.leftInChunk());
            buf[n.leftInChunk()] = '\0';
            click_chatter("'%s'",buf);
            n.moveToNextChunk();
        }

    }

private:
    /** @brief Repair a FlowBufferContentIter. After a deletion at the end of a packet,
     * the iterator may point after the new content of the packet and thus have an invalid position.
     * This method fixes it and ensures that the iterator point to the next packet
     */
    void repair();

    FlowBuffer *flowBuffer;
    Packet* entry;
    uint32_t offsetInPacket; // Current offset in the current packet content
};

class Chunk { public:
    const unsigned char* bytes;
    const unsigned length;
};

/** @class FlowBufferContentIter
 * @brief This iterator allows to iterate the content in the buffer seamlessly accross the packets
 */
class FlowBufferChunkIter
{
public:
    friend class FlowBuffer;

    /** @brief Construct a FlowBufferContentIter
     * @param _flowBuffer The FlowBuffer to which this iterator is linked
     * @param _entry The entry in the buffer to which this iterator points
     */
    inline FlowBufferChunkIter(FlowBuffer *_flowBuffer, Packet* packet);

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return True if the two iterators point to the same data in the flow
     */
    inline bool operator==(const FlowBufferChunkIter& other) const;

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return False if the two iterators point to the same data in the flow
     */
    inline bool operator!=(const FlowBufferChunkIter& other) const;

    /** @brief Return the byte to which this iterator points
     * @return The byte to which this iterator points
     */
    inline Chunk operator*();

    /** @brief Move the iterator to the next byte in the buffer
     * @return The iterator moved
     */
    inline FlowBufferChunkIter& operator++();

    inline operator bool() const {
        return entry != 0;
    }

    /**
     * Return all packets up to the current position, not included
     */
    inline PacketBatch* flush();

    inline Packet* current() {
        return entry;
    }

private:

    FlowBuffer *flowBuffer;
    Packet* entry;
};


// FlowBuffer Iterator
inline FlowBufferIter::FlowBufferIter(FlowBuffer *_flowBuffer,
    Packet* _entry) : flowBuffer(_flowBuffer)
{
    entry = static_cast<WritablePacket*>(_entry);
}

inline WritablePacket*& FlowBufferIter::operator*()
{
    assert(entry != NULL);

    return entry;
}

inline FlowBufferIter& FlowBufferIter::operator++()
{
    assert(entry != NULL);

    entry = static_cast<WritablePacket*>(entry->next());

    return *this;
}


inline bool FlowBufferIter::operator==(const FlowBufferIter& other) const
{
    if(this->flowBuffer != other.flowBuffer)
        return false;

    if(this->entry == NULL && other.entry == NULL)
        return true;

    if(this->entry != other.entry)
        return false;

    return true;
}

inline bool FlowBufferIter::operator!=(const FlowBufferIter& other) const
{
    return !(*this == other);
}

// FlowBufferContent Iterator
inline FlowBufferContentIter::FlowBufferContentIter(FlowBuffer *_flowBuffer,
    Packet* _entry, int posInFirstPacket) : flowBuffer(_flowBuffer), entry(_entry), offsetInPacket(posInFirstPacket)
{
    //The offset at first start must be valid
    while (entry && entry->getContentOffset() + offsetInPacket >=
        entry->length())
    {
        offsetInPacket = 0;
        entry = entry->next();
    }
}

inline bool FlowBufferContentIter::operator==(const FlowBufferContentIter& other) const
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

inline bool FlowBufferContentIter::operator!=(const FlowBufferContentIter& other) const
{
    return !(*this == other);
}

inline unsigned char& FlowBufferContentIter::operator*()
{
    unsigned char* content = static_cast<WritablePacket*>(entry)->getPacketContent();

    return *(content + offsetInPacket);
}


inline unsigned char* FlowBufferContentIter::get_ptr()
{
    unsigned char* content = static_cast<WritablePacket*>(entry)->getPacketContent();

    return content + offsetInPacket;
}


inline FlowBufferContentIter& FlowBufferContentIter::operator++()
{
    assert(entry != NULL);

    offsetInPacket++;

    // Check if we are at the end of the packet and must therefore switch to
    // the next one. We may have multiple empty packet
    while (entry && entry->getContentOffset() + offsetInPacket >=
        entry->length())
    {
        offsetInPacket = 0;
        entry = entry->next();
    }

    return *this;
}

inline FlowBufferContentIter& FlowBufferContentIter::operator+=(int p)
{
    assert(entry != NULL);

    while (entry->getContentOffset() + offsetInPacket + p >= entry->length()) {
        p -= entry->length() - entry->getContentOffset() + offsetInPacket; //Remove from p what was left in packet
        offsetInPacket = 0;
        entry = entry->next();
        if (!entry)
            return *this;
    }

    offsetInPacket += p;

    return *this;
}


inline PacketBatch* FlowBufferContentIter::flush() {
    if (entry == 0) {
        return this->flowBuffer->dequeueAll();
    } else {

        return this->flowBuffer->dequeueUpTo(entry);
    }
}


//FlowBufferChunkIter
inline FlowBufferChunkIter::FlowBufferChunkIter(FlowBuffer *_flowBuffer,
    Packet* _entry) : flowBuffer(_flowBuffer), entry(_entry)
{
    //The offset at first start must be valid
    while (entry && entry->getContentOffset() == entry->length())
    {
        entry = entry->next();
    }
}

inline bool FlowBufferChunkIter::operator==(const FlowBufferChunkIter& other) const
{
    if(this->flowBuffer != other.flowBuffer)
        return false;

    if(this->entry == NULL && other.entry == NULL)
        return true;

    if(this->entry != other.entry)
        return false;

    return true;
}

inline bool FlowBufferChunkIter::operator!=(const FlowBufferChunkIter& other) const
{
    return !(*this == other);
}

inline Chunk FlowBufferChunkIter::operator*()
{
    assert(entry != NULL);

    WritablePacket* wp = static_cast<WritablePacket*>(entry);
    int off = wp->getContentOffset();
    unsigned char* content = wp->data() + off;

    return Chunk{content, wp->length() - off};
}


inline FlowBufferChunkIter& FlowBufferChunkIter::operator++()
{
    assert(entry != NULL);

    entry = entry->next();
    //Advance while the entry have no content
    while (entry && entry->getContentOffset() == entry->length())
    {
        entry = entry->next();
    }

    return *this;
}


inline PacketBatch* FlowBufferChunkIter::flush() {
    if (entry == 0) {
        return this->flowBuffer->dequeueAll();
    } else {

        return this->flowBuffer->dequeueUpTo(entry);
    }
}


CLICK_ENDDECLS

#endif
