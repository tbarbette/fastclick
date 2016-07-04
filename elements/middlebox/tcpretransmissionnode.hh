#ifndef MIDDLEBOX_TCPRETRANSMISSION_NODE_HH
#define MIDDLEBOX_TCPRETRANSMISSION_NODE_HH

struct TCPRetransmissionNode
{
    Packet* packet;
    Timestamp lastTransmission;

    TCPRetransmissionNode()
    {

    }

    ~TCPRetransmissionNode()
    {

    }
};

#endif
