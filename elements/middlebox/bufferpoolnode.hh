#ifndef MIDDLEBOX_BUFFERPOOLNODE_HH
#define MIDDLEBOX_BUFFERPOOLNODE_HH

#include <click/vector.hh>

CLICK_DECLS

class BufferPoolNode
{
    friend class BufferPool;
public:
    BufferPoolNode(uint32_t initialSize);
    ~BufferPoolNode();

    unsigned char* getBuffer();
    uint32_t getSize();
    void resize(uint32_t newSize);

private:
    Vector<unsigned char> buffer;
    BufferPoolNode* nextNode;
};

CLICK_ENDDECLS

#endif
