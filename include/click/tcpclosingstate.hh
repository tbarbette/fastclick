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
        OPEN = 0, // The connection is open and nothing has been made to close it
        BEING_CLOSED_GRACEFUL_1, // The connection is being closed gracefully (via first FIN packets)
        BEING_CLOSED_GRACEFUL_2, // The connection is being closed gracefully (via second FIN packets, second should free after this)
//        BEING_CLOSED_UNGRACEFUL, // The connection is being closed ungracefully (via RST packets by one of the side which has now freed its state) -> NO NEED
        CLOSED // The connection has been closed (via RST packets or FIN, both side should have freed. Technically this state should be nearly unachievable)
    };
};

#endif
