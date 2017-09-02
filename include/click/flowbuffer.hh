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

class StackElement;

struct flowBufferEntry
{
        WritablePacket *packet;
        struct flowBufferEntry *prev;
        struct flowBufferEntry *next;
};

class FlowBufferContentIter;
class FlowBufferIter;
class StackElement;
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
    uint32_t getSize();

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
    int removeInFlow(const char* pattern, StackElement* remove);

    /** @brief Replace a pattern in the buffer
     * @param fcb A pointer to the FCB of this flow
     * @param The pattern to replace
     * @param The new data that will replace the old one
     * @return 1 if the pattern has been found and the first occurrence has been replaced
     * -1 if the pattern has not been found and cannot start in the last packet of the buffer
     * 0 if the pattern has not been found but the pattern could start at the end of the last
     * packet in the buffer and thus enqueuing the next packet could result in a match.
     */
    int replaceInFlow(const char* pattern, const char *replacement, StackElement* owner);

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
private:
    inline bool isInitialized() {
        return head != 0;
    }

    /** @brief Search a pattern in the buffer
     * @param start Content iterator indicating where to start the search
     * @param pattern The pattern to search
     * @param feedback An int in which the result of the search will be put (1 if found, -1 if
     * not found, 0 if not found but could start at the end of the last packet)
     * @return An iterator pointing to the beginning of the pattern in the flow if found
     * or after the end of the content if not found
     */
    FlowBufferContentIter search(FlowBufferContentIter start, const char* pattern, int *feedback);

    /** @brief Remove data in the flow (across the packets)
     * @param fcb A pointer to the FCB of the flow
     * @param start A content iterator pointing to the first byte to remove
     * @param length The number of bytes to remove
     */
    void remove(FlowBufferContentIter start, uint32_t length, StackElement* owner);

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
    FlowBufferIter(FlowBuffer *_flowBuffer, Packet* _entry);

    /** @brief Compare two FlowBufferIter
     * @param other The FlowBufferIter to be compared to
     * @return True if the two iterators point to the same packet
     */
    bool operator==(const FlowBufferIter& other) const;

    /** @brief Compare two FlowBufferIter
     * @param other The FlowBufferIter to be compared to
     * @return False if the two iterators point to the same packet
     */
    bool operator!=(const FlowBufferIter& other) const;

    /** @brief Return the packet to which this iterator points
     * @return The packet to which this iterator points
     */
    WritablePacket*& operator*();

    /** @brief Move the iterator to the next packet in the buffer
     * @return The iterator moved
     */
    FlowBufferIter& operator++();

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

    /** @brief Construct a FlowBufferContentIter
     * @param _flowBuffer The FlowBuffer to which this iterator is linked
     * @param _entry The entry in the buffer to which this iterator points
     */
    FlowBufferContentIter(FlowBuffer *_flowBuffer, Packet* packet, int posInFirstPacket=0);

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return True if the two iterators point to the same data in the flow
     */
    bool operator==(const FlowBufferContentIter& other) const;

    /** @brief Compare two FlowBufferContentIter
     * @param other The FlowBufferContentIter to be compared to
     * @return False if the two iterators point to the same data in the flow
     */
    bool operator!=(const FlowBufferContentIter& other) const;

    /** @brief Return the byte to which this iterator points
     * @return The byte to which this iterator points
     */
    unsigned char& operator*();

    /** @brief Move the iterator to the next byte in the buffer
     * @return The iterator moved
     */
    FlowBufferContentIter& operator++();

    inline operator bool() const {
        return entry;
    }

    /**
     * Return all packets up to the current position, not included
     */
    PacketBatch* flush();

    inline Packet* current() {
        return entry;
    }

private:
    /** @brief Repair a FlowBufferContentIter. After a deletion at the end of a packet,
     * the iterator may point after the new content of the packet and thus have an invalid position.
     * This method fixes it and ensures that the iterator point to the next packet
     */
    void repair();

    FlowBuffer *flowBuffer;
    Packet* entry;
    uint32_t offsetInPacket; // Current offset in the current packet
};

CLICK_ENDDECLS

#endif
