#ifndef MIDDLEBOX_CIRCULARBUFFER_HH
#define MIDDLEBOX_CIRCULARBUFFER_HH

#include <click/vector.hh>
#include "bufferpool.hh"
#include "bufferpoolnode.hh"

CLICK_DECLS

class CircularBuffer
{
public:
    CircularBuffer(BufferPool* bufferPool);
    ~CircularBuffer();

    uint32_t getSize();
    uint32_t getCapacity();
    uint32_t getStartOffset();
    void setStartOffset(uint32_t startOffset);
    bool isBlank();
    void removeDataAtBeginning(uint32_t newStart);
    void addDataAtEnd(const unsigned char* data, uint32_t length);
    void getData(uint32_t start, uint32_t length, Vector<unsigned char> &getBuffer);

private:
    BufferPool* bufferPool;
    BufferPoolNode* bufferNode;
    uint32_t bufferStart;
    uint32_t bufferEnd;
    uint32_t startOffset;
    uint32_t size;
    bool blank;
    bool useStartOffset;

    void increaseBufferSize(uint32_t addSize);
};

CLICK_ENDDECLS

#endif
