/*
 * retransmissiontiming.hh - Class used to manage the timings of TCP retransmissions.
 * Provides methods used to compute the RTT, retransmission timers, etc.
 *
 * Romain Gaillard.
 */

#ifndef MIDDLEBOX_RETRANSMISSIONTIMING_HH
#define MIDDLEBOX_RETRANSMISSIONTIMING_HH

#include <click/config.h>
#include <click/glue.hh>
#include <click/timer.hh>
#include <clicknet/tcp.h>
#include <click/timestamp.hh>
#include <click/circularbuffer.hh>
#include <click/memorypool.hh>

CLICK_DECLS

class TCPRetransmitter;

/**
 * Structure used to store information that the retransmission timer will pass when it fires
 */
struct retransmissionTimerData
{
    TCPRetransmitter *retransmitter; // TCPRetransmitter that owns this RetransmissionTiming
    //TODO struct fcb *fcb; // Pointer to the FCB of the flow
};

/**
 * @class RetransmissionTiming
 * @brief Class used to manage the timings of TCP retransmissions.
 * Provides methods used to compute the RTT, retransmission timers, etc.
 * It also stores a pointer to the circular buffer that contains the data waiting to be
 * (re)transmitted so that it can be accessed by both sides of a connection (as RetransmissionTiming
 * objects are contained in the tcp_common structure).
 */
class RetransmissionTiming
{
public:
    /**
     * @brief Construct a RetransmissionTiming
     */
    RetransmissionTiming();

    /**
     * @brief Destruct a RetransmissionTiming
     */
    ~RetransmissionTiming();

    inline void reinit() {
	this->~RetransmissionTiming();
	new (this) RetransmissionTiming();
    }
    /**
     * @brief Initialize the retransmission timer
     * @param fcb A pointer to the FCB of the flow
     * @param retransmitter A pointer to the TCPRetransmitter of this side of the flow
     */
    void initTimer(TCPRetransmitter *retransmitter);

    /**
     * @brief Return a boolean indicating whether the retransmission timer is initialized
     * @return A boolean indicating whether the retransmission timer is initialized
     */
    bool isTimerInitialized();

    /**
     * @brief Set the pointer to the circular buffer used by the TCPRetransmitter of this side
     * of the flow
     * @param buffer A pointer to the circular buffer
     * @param bufferPool The pool from which the CircularBuffer has been obtained. The
     * CircularBuffer will be put back automatically in the pool when the RetransmissionTiming
     * object is destructed.
     */
    void setCircularBuffer(CircularBuffer *buffer, MemoryPool<CircularBuffer> *bufferPool);

    /**
     * @brief Return a pointer to the circular buffer used by the TCPRetransmitter of this side
     * of the flow
     * @return Pointer to the circular buffer
     */
    CircularBuffer* getCircularBuffer();

    /**
     * @brief Start a RTT measure between the middlebox and the destination of this side of
     * the flow
     * @param seq The sequence number used for the RTT measure (the measure will be done when the
     * ack for this sequence number is received)
     * @return A boolean indicating whether the measure has been started. False means that another
     * measure is already in progress.
     */
    bool startRTTMeasure(uint32_t seq);

    /**
     * @brief Signal that a ACK has been received. This is used to stop RTT measures and
     * prune the circular buffer.
     * @param fcb A pointer to the FCB for this side of the flow
     * @param ack The ACK number received
     * @return A boolean indicating whether the RTT measure has been stopped. False means
     * that no measures were in progress
     */
    bool signalAck(uint32_t ack);

    /**
     * @brief Signal a retransmission. This is used to avoid measuring RTT for retransmitted packets
     * as suggested by Karn's algorithm
     * @param expectedAck The expected ACK number for the retransmission
     * @return A boolean indicating whether a measure was in progress
     */
    bool signalRetransmission(uint32_t expectedAck);

    /**
     * @brief Return a boolean indicating whether a RTT measure is in progress
     * @return A boolean indicating whether a RTT measure is in progress
     */
    bool isMeasureInProgress();

    /**
     * @brief Start the retransmission timer
     * @return A boolean indicating if the timer has been started
     */
    bool startTimer();

    /**
     * @brief Double the RTO and start the retransmission timer with the new value
     * @return A boolean indicating if the timer has been started
     */
    bool startTimerDoubleRTO();

    /**
     * @brief Stop the retransmission timer
     * @return A boolean indicating if the timer has been stopped
     */
    bool stopTimer();

    /**
     * @brief Restart the retransmission timer
     * @return A boolean indicating if the timer has been restarted
     */
    bool restartTimer();

    /**
     * @brief Return a boolean indicating whether the retransmission timer is running
     * @return A boolean indicating whether the retransmission timer is running
     */
    bool isTimerRunning();

    /**
     * @brief Fire the retransmission timer now (used for fast retransmission, generally after
     * receiving a determined number of duplicate ACKs)
     * @return A boolean indicating if the timer has been fired
     */
    bool fireNow();

    /**
     * @brief Try to send more data from the circular buffer to the destination
     * @return A boolean equal to false if it fails because the timer has not been initialized
     * (required for this operation)
     */
    bool sendMoreData();

    /**
     * @brief Return a boolean indicating whether this side of the connection has already sent
     * data manually to the source (it occurs when some data are ACKed by the middlebox and we are
     * then responsible for their (re)transmission)
     * @return A boolean indicating whether this side of the connection has already sent
     * data manually to the source
     */
    bool isManualTransmissionDone();

    /**
     * @brief Return the ack number we are expecting to receive for data transmitted manually
     * @return The ack number we are expecting to receive for data transmitted manually
     */
    uint32_t getLastManualTransmission();

    /**
     * @brief Set the ack number we are expecting to receive for data transmitted manually
     * @param lastManualTransmission The ack number we are expecting to receive for data
     * transmitted manually
     */
    void setLastManualTransmission(uint32_t lastManualTransmission);

    /**
     * @brief Method called when the retransmission timer fires. Must not be called manually
     * @param timer Pointer to the timer
     * @param data Data passed by the timer
     */
    static void timerRetransmitFired(Timer *timer, void *data);

    /**
     * @brief Method called when the thread timer fires (should occur immediately after a call
     * to sendMoreData)
     * @param timer Pointer to the timer
     * @param data Data passed by the timer
     */
    static void timerThreadFired(Timer *timer, void *data);

private:
    /**
     * @brief Compute the clock granularity (the smallest amount of time measurable for the RTT)
     */
    void computeClockGranularity();

    /**
     * @brief Ensure that the value of the RTO is not below its minimum value
     */
    void checkRTOMinValue();

    /**
     * @brief Ensure that the value of the RTO is not above its maximum value
     */
    void checkRTOMaxValue();

    // Retransmission timer
    Timer timerRetransmit; // Retransmission timer
    Timer timerThread; // Timer used to ensure that when we call "sendMoreData", it is executed
                       // in the right thread (not necessarily the thread calling the method but
                       // the thread responsible for this timer) by scheduling the timer
                       // immediately.
    struct retransmissionTimerData timerData; // Data passed by the retransmission timer
    TCPRetransmitter *owner;
    CircularBuffer *buffer;
    MemoryPool<CircularBuffer> *bufferPool;

    bool measureInProgress;
    Timestamp measureStartTime; // Timestamp at which we sent the packet used to measure the RTT
    Timestamp measureEndTime;

    uint32_t rttSeq; // Sequence number that started the last measure of the RTT

    uint32_t lastManualTransmission; // Stores the ACK to receive for data transmitted manually
    bool manualTransmissionDone; // Indicates whether we already sent data manually

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
};

CLICK_ENDDECLS

#endif
