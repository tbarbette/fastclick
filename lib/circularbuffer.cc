/*
 * circularbuffer.cc - Class used to represent a circular buffer with a dynamic size.
 * The operations are O(1) and the buffer automatically grows to be able to store
 * the data.
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <click/circularbuffer.hh>

CLICK_DECLS

CircularBuffer::CircularBuffer(BufferPool* bufferPool)
{
    this->bufferPool = bufferPool;
    // Obtain a buffer node from the memory pool in order to have memory for this circular buffer
    this->bufferNode = bufferPool->getBuffer();
    bufferStart = 0;
    bufferEnd = 0;
    startOffset = 0;
    size = 0;
    blank = true;
    useStartOffset = false;
}

CircularBuffer::~CircularBuffer()
{
    // Put back the buffer node into its memory pool. We got this memory in the constructor
    bufferPool->releaseBuffer(bufferNode);
}

void CircularBuffer::increaseBufferSize(uint32_t addSize)
{
    uint32_t prevSize = bufferNode->getSize();
    // Resize the buffer itself
    bufferNode->resize(prevSize + addSize);

    // Ensure that the circular buffer is still valid
    if((bufferEnd < bufferStart && size == 0) || (bufferEnd <= bufferStart && size > 0))
    {
        // In this case, the end of the buffer has an index less than
        // the start, meaning that we indeed have a circularity in the buffer
        // This case is problematic when we increase the size of the buffer
        // has it means that some new elements would be added between the start
        // and the end.
        // To overcome this problem, we simply move the elements between the start
        // position and the previous end of the buffer to the right
        uint32_t nbElemToMove = prevSize - bufferStart;
        uint32_t newStart = bufferStart + addSize;
        unsigned char* buffer = bufferNode->getBuffer();

        memmove(&buffer[newStart], &buffer[bufferStart], nbElemToMove);
        // Define the new start of the buffer
        bufferStart = newStart;
    }
    // If the end is after the start, the new elements are not added
    // between the start and the end, but after the end. So this is not problematic.

    click_chatter("Warning: buffer for tcp retransmission needed more space (%u)", addSize);
}

uint32_t CircularBuffer::getSize()
{
    return size;
}

uint32_t CircularBuffer::getCapacity()
{
    return bufferNode->getSize();
}

void CircularBuffer::removeDataAtBeginning(uint32_t newStart)
{
    // Count the number of elements to remove
    // newStart is a sequence number so we need to map it to a position in the array
    uint32_t nbRemoved = (newStart - getStartOffset());

    if(nbRemoved == 0)
        return;

    // Ensure that we do not remove too much data
    if(nbRemoved > getSize())
        nbRemoved = getSize();

    // Update the offset of the first byte of data in the buffer
    if(useStartOffset)
        setStartOffset(getStartOffset() + nbRemoved);

    bufferStart += nbRemoved;

    // Ensure that the new starting position is not outside of the buffer
    if(bufferStart >= getCapacity())
        bufferStart = bufferStart - getCapacity();

    size -= nbRemoved;
}

void CircularBuffer::addDataAtEnd(const unsigned char* data, uint32_t length)
{
    blank = false;

    // Check that the buffer is large enough, otherwise increase its size
    if(getSize() + length >= getCapacity())
        increaseBufferSize(getSize() + length - getCapacity() + 1);

    uint32_t addPosition = bufferEnd;

    bufferEnd += length;

    // Check whether the end pointer needs to wrap
    if(bufferEnd >= getCapacity())
        bufferEnd = bufferEnd - getCapacity();

    // There are two cases possible when we add data in the circular buffer.
    // Either all the data fit between the start pointer and the end of the array used for the
    // buffer and no wrap is needed.
    // Or the end of the data must be added at the beginning of the array because we have a wrap.
    // E.g. we have an array of size 10, the start pointer is set to 6 and the end pointer is set
    // to 3. We want to add 6 bytes in the buffer. We will add the first 4 bytes in the array
    // at the positions [6, 9] and the last two bytes at the positions [0, 1] of the array.
    uint32_t firstEnd = addPosition + length;
    uint32_t remainToAdd = 0;
    uint32_t firstAddLength = length;
    if(firstEnd >= getCapacity())
    {
        remainToAdd = firstEnd - getCapacity();
        firstAddLength = length - remainToAdd;
    }

    unsigned char* buffer = bufferNode->getBuffer();
    memcpy(&buffer[addPosition], data, firstAddLength);

    // Check if there are still data to add at the beginning of the buffer because it wrapped
    if(remainToAdd > 0)
        memcpy(&buffer[0], &data[firstAddLength], remainToAdd);

    size += length;
}

void CircularBuffer::setStartOffset(uint32_t startOffset)
{
    this->startOffset = startOffset;
    useStartOffset = true;
}

uint32_t CircularBuffer::getStartOffset()
{
    return startOffset;
}

bool CircularBuffer::isBlank()
{
    return blank;
}


void CircularBuffer::getData(uint32_t start, uint32_t length, Vector<unsigned char> &getBuffer)
{
    // Check that the data are in the buffer
    if(SEQ_LT(start, getStartOffset()) || start - getStartOffset() > getSize())
    {
        click_chatter("Error: TCPRetransmission: data not in the buffer.");
        return;
    }

    // Check if we really requested data
    if(length == 0)
    {
        // If no data requested, set the "get buffer"'s size to 0
        getBuffer.resize(0);
        return;
    }

    // "Start" is a sequence number so we need to map it to a position
    start = start - getStartOffset() + bufferStart;

    // Check that we do not request too much data
    if(length > getSize())
        length = getSize();

    // Ensure that the "get buffer" has the right size to contain the data
    getBuffer.resize(length);

    // Get the first part of the data (at the end of the array)
    uint32_t firstLength = length;
    uint32_t secondLength = 0;
    if(start + firstLength >= getCapacity())
    {
        firstLength = getCapacity() - start;
        secondLength = length - firstLength;
    }
    unsigned char* buffer = bufferNode->getBuffer();

    memcpy(&getBuffer[0], &buffer[start], firstLength);

    // Potentially get the rest of the data (at the beginning of the array)
    if(secondLength > 0)
        memcpy(&getBuffer[firstLength], &buffer[0], secondLength);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
ELEMENT_PROVIDES(CircularBuffer)
