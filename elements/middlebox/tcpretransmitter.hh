#ifndef MIDDLEBOX_TCPRETRANSMITTER_HH
#define MIDDLEBOX_TCPRETRANSMITTER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>
#include <click/timer.hh>
#include <click/timestamp.hh>
#include <click/vector.hh>
#include <clicknet/tcp.h>
#include "stackelement.hh"
#include "memorypool.hh"
#include "bufferpool.hh"
#include "tcpelement.hh"
#include "bufferpoolnode.hh"
#include "bytestreammaintainer.hh"

CLICK_DECLS

#define TCPRETRANSMITTER_BUFFER_NUMBER 10
#define TCPRETRANSMITTER_GET_BUFFER_SIZE 1500

class TCPRetransmitter : public StackElement, public TCPElement
{
public:
    TCPRetransmitter() CLICK_COLD;
    ~TCPRetransmitter() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "TCPRetransmitter"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }
    void push(int port, Packet *packet);
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    bool isOutElement()                   { return true; }

    void retransmissionTimerFired(struct fcb* fcb);

    void signalAck(struct fcb* fcb, uint32_t ack);

private:
    BufferPool *rawBufferPool;
    MemoryPool<CircularBuffer> circularPool;
    Vector<unsigned char> getBuffer;

    void prune(struct fcb *fcb);
    bool dataToRetransmit(struct fcb *fcb);
};

CLICK_ENDDECLS

#endif
