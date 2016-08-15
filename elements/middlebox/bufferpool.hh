/*
 * bufferpool.hh - Class used to provide a pool of buffers that can have a dynamic size
 * and be resized at any moment.
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_BUFFERPOOL_HH
#define MIDDLEBOX_BUFFERPOOL_HH

#include "bufferpoolnode.hh"

CLICK_DECLS

/** @class BufferPool
 * @brief Class used to provide a pool of buffers that can have a dynamic size
 * and be resized at any moment
 *
 * Contrarily to MemoryPool, BufferPool provides a pool of buffers that can have a dynamic size
 * and be resized at any moment. The drawback is that the BufferPool does not directly
 * return a chunk of memory (as MemoryPool does) but a pointer to a BufferPoolNode that contains
 * the memory chunk. The pointer to the BufferPoolNode must also be used to release memory.
 */
class BufferPool
{
public:
    /** @brief Construct a BufferPool
     * @param initialNumber The initial number of buffers in the pool
     * @param initialSize The initial size of the buffers in the pool
     */
    BufferPool(uint32_t initialNumber, uint32_t initialSize);

    /** @brief Destruct a BufferPool and free the memory
     */
    ~BufferPool();

    /** @brief Return a buffer from the pool. Note that the size of the buffer is random, so be
     * sure to resize it to fit your needs.
     * @return A pointer to a BufferPoolNode used to manage the buffer
     */
    BufferPoolNode* getBuffer();

    /** @brief Put back a buffer into the pool
     * @param buffer A pointer to the BufferPoolNode used to manage the buffer
     */
    void releaseBuffer(BufferPoolNode* buffer);

private:
    BufferPoolNode *head; /** First BufferPoolNode available */
    uint32_t initialSize; /** Initial size of the buffers */

    /** @brief Allocate more buffers in the pool
     * @param n The number of buffers to allocate
     */
    void allocateMoreBuffers(uint32_t n);

};

CLICK_ENDDECLS

#endif
