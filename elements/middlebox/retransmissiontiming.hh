#ifndef MIDDLEBOX_RETRANSMISSIONTIMING_HH
#define MIDDLEBOX_RETRANSMISSIONTIMING_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include <click/timestamp.hh>
#include "circularbuffer.hh"
#include "memorypool.hh"

CLICK_DECLS


class TCPRetransmitter;
struct fcb;

struct retransmissionTimerData
{
    TCPRetransmitter *retransmitter;
    struct fcb *fcb;
};

class RetransmissionTiming
{
public:
    RetransmissionTiming();
    ~RetransmissionTiming();

    void initTimer(struct fcb* fcb, TCPRetransmitter *retransmitter);
    bool isTimerInitialized();

    void setCircularBuffer(CircularBuffer *buffer, MemoryPool<CircularBuffer> *bufferPool);
    CircularBuffer* getCircularBuffer();

    bool startRTTMeasure(uint32_t seq);
    bool signalAck(struct fcb* fcb, uint32_t ack);
    bool signalRetransmission(uint32_t expectedAck);
    bool isMeasureInProgress();

    bool startTimer();
    bool startTimerDoubleRTO();
    bool stopTimer();
    bool restartTimer();
    bool restartTimerNow();
    bool isTimerRunning();

    bool isManualTransmissionDone();
    uint32_t getLastManualTransmission();
    void setLastManualTransmission(uint32_t lastManualTransmission);

    static void timerFired(Timer *timer, void *data);

private:
    // Retransmission timer
    Timer timer;
    struct retransmissionTimerData timerData;
    TCPRetransmitter *owner;
    CircularBuffer *buffer;
    MemoryPool<CircularBuffer> *bufferPool;

    bool measureInProgress;
    Timestamp measureStartTime;
    Timestamp measureEndTime;

    uint32_t rttSeq; // Sequence number that started the last measure of the RTT

    uint32_t lastManualTransmission;
    bool manualTransmissionDone;

    // Statistics about the retransmission timing
    // These variables are expressed in milliseconds
    uint32_t srtt;               // Smoothed Round-trip Time
    uint32_t rttvar;             // Round-trip time variation
    uint32_t rto;                // Retransmission TimeOut
    uint32_t clockGranularity;   // Smallest amount of time measurable

    const uint32_t K = 4;
    // (Jacobson, V., "Congestion Avoidance and Control")
    const float ALPHA = 1.0/8;
    const float BETA = 1.0/4;

    void computeClockGranularity();
    void checkRTOMinValue();
    void checkRTOMaxValue();
};

CLICK_ENDDECLS

#endif
