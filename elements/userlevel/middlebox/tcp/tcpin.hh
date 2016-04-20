#ifndef MIDDLEBOX_TCPIN_HH
#define MIDDLEBOX_TCPIN_HH
#include "tcpelement.hh"
#include "tcpout.hh"
#include <click/element.hh>
CLICK_DECLS

class TCPIn : public TCPElement
{
public:
    TCPIn() CLICK_COLD;

    const char *class_name() const        { return "TCPIn"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    TCPOut* getOutElement();
    TCPIn* getReturnElement();

    enum ClosingState
    {
        OPEN,           // The connection is open and nothing has been made to close it
        FIN_WAIT,       // A FIN packet has been sent to the host
        FIN_WAIT2,      // The ACK has been received from the host
        CLOSED          // The FINACK has been received from the host and an ACK has been sent. The connection is closed
    };

protected:
    Packet* processPacket(Packet*);
    void packetModified(Packet*);
    void closeConnection(uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool);
    void closeConnection(uint32_t, uint32_t, uint16_t, uint16_t, tcp_seq_t, tcp_seq_t, bool, bool);

    TCPOut* outElement;
    TCPIn* returnElement;
    ClosingState closingState;


};

CLICK_ENDDECLS
#endif
