#ifndef MIDDLEBOX_TCPREORDER_NODE_HH
#define MIDDLEBOX_TCPREORDER_NODE_HH

struct TCPPacketListNode
{
    Packet* packet;
    struct TCPPacketListNode* next;
};

#endif
