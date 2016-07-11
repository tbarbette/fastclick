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
