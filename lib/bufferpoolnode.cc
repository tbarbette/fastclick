/*
 * bufferpoolnode.cc - Class used to manage a buffer obtained via a BufferPool
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/bufferpoolnode.hh>

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

uint32_t BufferPoolNode::getSize() const
{
    return buffer.size();
}

void BufferPoolNode::resize(uint32_t newSize)
{
    // Resize the vector to have the requested size and set the value of the potential new elements
    // to 0
    buffer.resize(newSize, '\0');
}

CLICK_ENDDECLS
