#ifndef MIDDLEBOX_TCPCLOSINGSTATE_HH
#define MIDDLEBOX_TCPCLOSINGSTATE_HH

// Using a structure to avoid polluting the namespace
// We must therefore use for instance TCPClosingState::OPEN
struct TCPClosingState
{
    enum Value
    {
        OPEN,           // The connection is open and nothing has been made to close it
        BEING_CLOSED_GRACEFUL,
        CLOSED_GRACEFUL,
        BEING_CLOSED_UNGRACEFUL,
        CLOSED_UNGRACEFUL
    };
};

#endif
