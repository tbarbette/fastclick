/*
 * bufferpoolnode.hh - Class used to manage a buffer obtained via a BufferPool
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_BUFFERPOOLNODE_HH
#define MIDDLEBOX_BUFFERPOOLNODE_HH

#include <click/vector.hh>

CLICK_DECLS

/** @class BufferPoolNode
 * @brief Class used to manage a buffer obtained via a BufferPool
 *
 * BufferPool returns a pointer to a BufferPoolNode that is used to manage the buffer
 * and perform operations such as resize the buffer.
 */
class BufferPoolNode
{
    friend class BufferPool;
public:
    /** @brief Construct a BufferPoolNode
     * @param initialSize The initial size of the buffer
     */
    BufferPoolNode(uint32_t initialSize);

    /** @brief Destruct a BufferPoolNode
     */
    ~BufferPoolNode();

    /** @brief Return the actual buffer (the area of memory).
     * Note that the size of the buffer is random, so be
     * sure to resize it to fit your needs.
     * @return A pointer to the buffer managed by this BufferPoolNode
     */
    unsigned char* getBuffer();

    /** @brief Return the size of the buffer
     * @return The size of the buffer
     */
    uint32_t getSize() const;

    /** @brief Resize the buffer. This operation is efficient if the capacity of the buffer
     * is greater than the requested size. The capacity of the buffer depends on its previous
     * sizes.
     * @param newSize The new size of the buffer. If the buffer grows, the new memory will be
     * set to 0.
     */
    void resize(uint32_t newSize);

private:
    // We use a Vector as it provides a contiguous area of memory that can be resized at will
    Vector<unsigned char> buffer; /** Chunk of memory representing the buffer */
    BufferPoolNode* nextNode; /** Next BufferPoolNode in the BufferPool */
};

CLICK_ENDDECLS

#endif
