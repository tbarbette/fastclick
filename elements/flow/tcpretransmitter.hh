#ifndef MIDDLEBOX_TCPRETRANSMITTER_HH
#define MIDDLEBOX_TCPRETRANSMITTER_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/element.hh>
#include <click/timer.hh>
#include <click/timestamp.hh>
#include <click/sync.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <clicknet/tcp.h>
#include "stackelement.hh"
#include <click/memorypool.hh>
#include <click/bufferpool.hh>
#include <click/tcpelement.hh>
#include "tcpin.hh"
#include <click/bufferpoolnode.hh>
#include <click/bytestreammaintainer.hh>

CLICK_DECLS

/*
=c

TCPRetransmitter([INITIALBUFFERSIZE])

=s middlebox

manages the tcp retransmissions and ensures that data we are responsible for (data we ACKed) are
correctly received and that their transmission is done correctly, using the tcp mechanisms
such as slow start.

=d

This element stores the data we are transmitting until it is ACKed in order to be retransmitted
if needed. When packets are modified by the middlebox, this is required as we must not reprocess
retransmitted packets but instead, retransmit the corresponding processed data.

This element also ensures that the data we are now responsible for (data we ACKed via the
requestMorePackets method) and that is not managed by the source anymore will be received correctly
by the destination (because we ACKed them, the source will never retransmit them as it thinks the
destination received them) and that we transmit these data correctly, following the TCP requirements
such as taking into account the congestion window and the receiver's window size. This is important
because an element can bufferize a large number of packets and flush the buffer suddently. This
element avoids that, in this case, an amount of data too large for the network or the receiver
is sent naively. For this purpose, it implements TCP Tahoe for the transmission of these
data.

=item INITIALBUFFERSIZE

Initial size of the circular buffers used to store the data waiting to be ACKed.
Default value: 65535

=a TCPIn, TCPOut, TCPReorder */

#define TCPRETRANSMITTER_BUFFER_NUMBER 10
#define TCPRETRANSMITTER_GET_BUFFER_SIZE 1500
#define MAX_TRANSMIT 65535 - 40 // Max size of the content of a packet (minus 40 bytes for the headers)

class TCPRetransmitter : public StackElement, public TCPElement
{
public:
    /**
     * @brief Construct a TCPRetransmitter element
     */
    TCPRetransmitter() CLICK_COLD;

    /**
     * @brief Destruct a TCPRetransmitter element
     */
    ~TCPRetransmitter() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "TCPRetransmitter"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *) CLICK_COLD;

    void push_batch(int port, PacketBatch *batch);

    /**
     * @brief Called when the retransmission timer fired. Will retransmit the corresponding data
     * @param fcb A pointer to the FCB of the flow
     */
    void retransmissionTimerFired();

    /**
     * @brief Transmit more data we are responsible for from the buffer. Called after previously sent
     * data have been ACKed.
     * @param fcb A pointer to the FCB of the flow
     */
    void transmitMoreData();

    /**
     * @brief Signal that we received an ACK. It will prune the buffer, manage the retransmission
     * timer and potentially send more data we are responsible for.
     * @param fcb A pointer to the FCB of the flow
     * @param ack Number of the last ack received
     */
    void signalAck(uint32_t ack);

private:

    /**
     * @brief Prune the circular buffer. Used when data are ACKed by the destination
     * @param fcb A pointer to the FCB of the flow
     */
    void prune();

    /**
     * @brief Indicate if there are data we are responsible for to retransmit
     * @param fcb A pointer to the FCB of the flow
     * @return A boolean indicating if the buffer contains data we are responsible for to retransmit
     */
    bool dataToRetransmit();

    /**
     * @brief Ensure that the component is correctly initialized
     * @param fcb A pointer to the FCB of the flow
     */
    void checkInitialization();

    /**
     * @brief Process a packet coming from the stack of the middlebox (on the first input)
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to process
     * @return A pointer to the processed packet
     */
    Packet* processPacketNormal(Packet *packet);

    /**
     * @brief Process a retransmitted packet (coming on the second input)
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to process
     * @return A pointer to the processed packet
     */
    Packet* processPacketRetransmission(Packet *packet);

    /**
     * @brief Transmit data we are responsible for, currently waiting in the buffer. It will ensure
     * that we do not transmit an amount of data too large for the network or the receiver.
     * @param fcb A pointer to the FCB of the flow
     * @param retransmission A boolean indicating whether these data have already been transmitted
     * before
     * @return A boolean indicating whether data have been sent
     */
    bool manualTransmission(bool retransmission);

    /**
     * @brief Return the maximum amount of data we can send in order to respect the congestion
     * window and the receiver's window
     * @param fcb A pointer to the FCB of the flow
     * @param expected The amount of data we want to send
     * @param canCut A boolean indicating if the data can be split. If canCut is false, the method
     * will either return "expected" if possible, or 0 if the expected amount is too large. If
     * canCut is true, the method can return any amount in the interval [0, expected]
     * @return The amount of data we can send
     */
    uint32_t getMaxAmountData(uint32_t expected, bool canCut);

    per_thread<BufferPool*> rawBufferPool;
    per_thread<MemoryPool<CircularBuffer>> circularPool;
    per_thread<Vector<unsigned char>> getBuffer;

    TCPIn* _in;
};

CLICK_ENDDECLS

#endif
