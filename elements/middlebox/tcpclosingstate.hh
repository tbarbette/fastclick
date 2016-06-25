#ifndef MIDDLEBOX_TCPCLOSINGSTATE_HH
#define MIDDLEBOX_TCPCLOSINGSTATE_HH

// Using a structure to avoid polluting the namespace
// We must therefore use for instance TCPClosingState::OPEN
struct TCPClosingState
{
    enum Value
    {
        OPEN,           // The connection is open and nothing has been made to close it
        FIN_WAIT,       // A FIN packet has been sent to the host
        FIN_WAIT2,      // The ACK has been received from the host
        CLOSED          // The FINACK has been received from the host and an ACK has been sent. The connection is closed
    };
};

#endif
