#ifndef MIDDLEBOX_BUFFERPOOL_HH
#define MIDDLEBOX_BUFFERPOOL_HH

#include "bufferpoolnode.hh"

CLICK_DECLS

class BufferPool
{
public:
    BufferPool(uint32_t initialNumber, uint32_t initialSize);
    ~BufferPool();

    BufferPoolNode* getBuffer();
    void releaseBuffer(BufferPoolNode*);


private:
    BufferPoolNode *head;
    uint32_t initialSize;

    void allocateMoreBuffers(uint32_t n);

};

CLICK_ENDDECLS

#endif
