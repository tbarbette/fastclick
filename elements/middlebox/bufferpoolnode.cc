#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include "bufferpoolnode.hh"

CLICK_DECLS

BufferPoolNode::BufferPoolNode(uint32_t initialSize) : buffer(initialSize, '\0')
{

}

BufferPoolNode::~BufferPoolNode()
{

}

unsigned char* BufferPoolNode::getBuffer()
{
    return (unsigned char*)&buffer[0];
}

uint32_t BufferPoolNode::getSize()
{
    return buffer.size();
}

void BufferPoolNode::resize(uint32_t newSize)
{
    buffer.resize(newSize, '\0');
}

CLICK_ENDDECLS

ELEMENT_PROVIDES(BufferPoolNode)
