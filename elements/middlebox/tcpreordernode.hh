/*
 * tcpreordernode.hh -- This structure is used to represent a node in the list of waiting packets
 * used by TCPReorder
 * Romain Gaillard
 *
 */

#ifndef MIDDLEBOX_TCPREORDER_NODE_HH
#define MIDDLEBOX_TCPREORDER_NODE_HH

struct TCPPacketListNode
{
    Packet* packet;
    struct TCPPacketListNode* next;
};

#endif
