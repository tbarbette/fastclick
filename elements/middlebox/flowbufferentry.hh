/*
 * flowbufferentry.hh - Structure that represents an entry in the FlowBuffer.
 * An entry consists of a packet that contains some data of the flow, a pointer to the previous
 * flowBufferEntry and a pointer to the next one. It is thus a double linked list.
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_FLOWBUFFERENTRY_HH
#define MIDDLEBOX_FLOWBUFFERENTRY_HH

#include <click/packet.hh>

struct flowBufferEntry
{
    WritablePacket *packet;
    struct flowBufferEntry *prev;
    struct flowBufferEntry *next;
};

#endif
