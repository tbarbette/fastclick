/*
 * TCPState.hh - Enumeration used to store the closing state of a side of a TCP connection.
 * Each side of the connection must have its own state as one may be closed while the other is
 * still open
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_TCPState_HH
#define MIDDLEBOX_TCPState_HH

// Using a structure to avoid polluting the namespace
// We must therefore use for instance TCPState::OPEN
struct TCPState
{
    enum Value
    {
        ESTABLISHING_1 = 0,
        ESTABLISHING_2,
        OPEN, // The connection is open and nothing has been made to close it
        BEING_CLOSED_GRACEFUL_1, // The connection is being closed gracefully (via first FIN packets)
        BEING_CLOSED_ARTIFICIALLY_1,
        BEING_CLOSED_ARTIFICIALLY_2,
        BEING_CLOSED_GRACEFUL_2, // The connection is being closed gracefully (via second FIN packets, second should free after this)
        CLOSED, // The connection has been closed (via RST packets or FIN, both side should have freed. Technically this state should be nearly unreadable)
    };
};

#endif
