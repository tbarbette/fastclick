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

    bool isOutElement()                   { return true; }

    // Click related methods
    const char *class_name() const        { return "TCPRetransmitter"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_packet(int port, Packet *packet);
    #if HAVE_BATCH
    void push_batch(int port, PacketBatch *batch);
    #endif

    void retransmissionTimerFired(struct fcb* fcb);
    void transmitMoreData(struct fcb* fcb);

    void signalAck(struct fcb* fcb, uint32_t ack);

private:
    // Will be associated to the thread managing this direction of the flow as a TCPRetransmitter
    // element is responsible for a direction of the flow and thus used by only one thread
    BufferPool *rawBufferPool;
    MemoryPool<CircularBuffer> circularPool;
    Vector<unsigned char> getBuffer;

    void prune(struct fcb *fcb);
    bool dataToRetransmit(struct fcb *fcb);
    void checkInitialization(struct fcb *fcb);
    Packet* processPacketNormal(struct fcb *fcb, Packet *packet);
    Packet* processPacketRetransmission(struct fcb *fcb, Packet *packet);
    bool manualTransmission(struct fcb *fcb, bool retransmission);
    uint16_t getMaxAmountData(struct fcb *fcb, uint16_t expected, bool canCut);
};

CLICK_ENDDECLS

#endif
