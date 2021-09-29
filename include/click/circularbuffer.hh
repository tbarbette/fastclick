/*
 * circularbuffer.hh - Class used to represent a circular buffer with a dynamic size.
 * The operations on it are O(1) and the buffer automatically grows to be able to store
 * the data.
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_CIRCULARBUFFER_HH
#define MIDDLEBOX_CIRCULARBUFFER_HH

#include <click/vector.hh>
#include <click/bufferpool.hh>
#include <click/bufferpoolnode.hh>

CLICK_DECLS

 /** @class CircularBuffer
  * @brief Represents a ring buffer that automatically grows to be able to store the data
  * and provides O(1) operations. The buffer uses a buffer pool from which it gets an initial
  * memory chunk that it will keep big enough to store the data.
  */
class CircularBuffer
{
public:
    /** @brief Construct a CircularBuffer
     * @param bufferPool The BufferPool from which the buffer will be retrieved
     */
    CircularBuffer(BufferPool* bufferPool);

    /** @brief Destruct a CircularBuffer
     * Put the buffer back into the BufferPool
     */
    ~CircularBuffer();

    /** @brief Return the size of the circular buffer (the size of the data in it)
     * @return Size of the circular buffer
     */
    uint32_t getSize();

    /** @brief Return the capacity of the circular buffer (the size of the memory chunk it uses)
     * @return Capacity of the circular buffer
     */
    uint32_t getCapacity();

    /** @brief Return the start offset. Offset of the first byte in the buffer. Used to be able to
     * manipulate sequence numbers. This offset is substracted from the requested data's position in
     * order to obtain a position in the buffer.
     * @return The start offset
     */
    uint32_t getStartOffset();

    /** @brief Set the start offset. Offset of the first byte in the buffer. Used to be able to
     * manipulate sequence numbers. This offset is substracted from the requested data's position in
     * order to obtain a position in the buffer.
     * @param startOffset The start offset
     */
    void setStartOffset(uint32_t startOffset);

    /** @brief Indicate whether the buffer is blank
     * @return True if the buffer is blank
     */
    bool isBlank();

    /** @brief Remove data at the beginning of the buffer
     * @param newStart Position of the new first byte in the buffer (takes into account the start
     * offset)
     */
    void removeDataAtBeginning(uint32_t newStart);

    /** @brief Add data at the end of the buffer
     * @param data The data to add
     * @param length The length of the data to add
     */
    void addDataAtEnd(const unsigned char* data, uint32_t length);

    /** @brief Retrieve data from the buffer
     * @param start Position of the first byte (takes into account the start offset)
     * @param length The length of the data to retrieve
     * @param getBuffer Buffer in which the data will be returned. Its size will be updated
     */
    void getData(uint32_t start, uint32_t length, Vector<unsigned char> &getBuffer);

private:
    BufferPool* bufferPool;
    BufferPoolNode* bufferNode;
    uint32_t bufferStart; // Position in the buffer of the beginning of the data
    uint32_t bufferEnd; // Position in the buffer of the end of the data
    uint32_t startOffset;
    uint32_t size; // Current size of the circular buffer (not necessarily the size of the
                   // buffer in the BufferPoolNode, which is the capacity instead).
    bool blank;
    bool useStartOffset; // Indicates whether we use the start offset or not

    /** @brief Increase the size of the circular buffer
     * @param addSize The number of bytes to add
     */
    void increaseBufferSize(uint32_t addSize);
};

CLICK_ENDDECLS

#endif
