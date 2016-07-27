/*
 * tcpclosingstate.hh - Enumeration used to store the closing state of a side of a TCP connection.
 * Each side of the connection must have its own state as one may be closed while the other is
 * still open
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_TCPCLOSINGSTATE_HH
#define MIDDLEBOX_TCPCLOSINGSTATE_HH

// Using a structure to avoid polluting the namespace
// We must therefore use for instance TCPClosingState::OPEN
struct TCPClosingState
{
    enum Value
    {
        OPEN, // The connection is open and nothing has been made to close it
        BEING_CLOSED_GRACEFUL, // The connection is being closed gracefully (via FIN packets)
        CLOSED_GRACEFUL, // The connection has been closed gracefully (via FIN packets)
        BEING_CLOSED_UNGRACEFUL, // The connection is being closed ungracefully (via RST packets)
        CLOSED_UNGRACEFUL // The connection has been closed ungracefully (via RST packets)
    };
};

#endif
