#include <click/config.h>
#include <click/glue.hh>
#include "bufferpool.hh"

CLICK_DECLS

BufferPool::BufferPool(uint32_t initialNumber, uint32_t initialSize)
{
    head = NULL;
    this->initialSize = initialSize;
    allocateMoreBuffers(initialNumber);
}

BufferPool::~BufferPool()
{
    BufferPoolNode* current = head;
    BufferPoolNode* toDelete = NULL;
    while(current != NULL)
    {
        toDelete = current;
        current = current->nextNode;

        delete toDelete;
    }
}

void BufferPool::allocateMoreBuffers(uint32_t n)
{
    for(int i = 0; i < n; ++i)
    {
        BufferPoolNode* newBuffer = new BufferPoolNode(initialSize);

        newBuffer->nextNode = head;
        head = newBuffer;
    }
}

void BufferPool::releaseBuffer(BufferPoolNode *buffer)
{
    buffer->nextNode = head;
    head = buffer;
}

BufferPoolNode* BufferPool::getBuffer()
{
    if(head == NULL)
        allocateMoreBuffers(1);

    BufferPoolNode* buffer = head;

    head = buffer->nextNode;

    return buffer;
}

CLICK_ENDDECLS

ELEMENT_PROVIDES(BufferPool)
ELEMENT_REQUIRES(BufferPoolNode)
