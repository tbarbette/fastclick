/*
 * retransmissiontiming.cc - Class used to manage the timings of TCP retransmissions.
 * Provides methods used to compute the RTT, retransmission timers, etc.
 *
 * Romain Gaillard.
 */

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include "retransmissiontiming.hh"
#include "tcpretransmitter.hh"

CLICK_DECLS

RetransmissionTiming::RetransmissionTiming()
{
    computeClockGranularity();
    srtt = 0;
    rttvar = 0;
    rto = 3000; // RFC 1122
    measureInProgress = false;
    owner = NULL;
    buffer = NULL;
    bufferPool = NULL;
    lastManualTransmission = 0;
    manualTransmissionDone = false;
}

RetransmissionTiming::~RetransmissionTiming()
{
    stopTimer();

    if(buffer != NULL && bufferPool != NULL)
    {
        // Release memory for the circular buffer
        buffer->~CircularBuffer();
        bufferPool->releaseMemory(buffer);
    }
}

void RetransmissionTiming::computeClockGranularity()
{
    // Determine the smallest amount of time measurable for the RTT values

    // We first check the epsilon of the timestamps
    uint32_t epsilon = Timestamp::epsilon().msec();

    // We then check the precision of the timers
    uint32_t timerAdjustment = timerRetransmit.adjustment().msec();

    // We select the highest value between both of them
    if(epsilon < timerAdjustment)
        clockGranularity = timerAdjustment;
    else
        clockGranularity = epsilon;
}

void RetransmissionTiming::initTimer(TCPRetransmitter *retransmitter)
{
    owner = retransmitter;
    timerRetransmit.initialize((Element*)retransmitter);
    timerThread.initialize((Element*)retransmitter);
    // Assign the callback of the timer
    // Give it a pointer to the fcb so that when the timer fires, we
    // can access it
    timerData.retransmitter = retransmitter;
    //TODO timerData.fcb = fcb;
    timerRetransmit.assign(RetransmissionTiming::timerRetransmitFired, (void*)&timerData);
    timerThread.assign(RetransmissionTiming::timerThreadFired, (void*)&timerData);
}

void RetransmissionTiming::setCircularBuffer(CircularBuffer *buffer,
    MemoryPool<CircularBuffer> *bufferPool)
{
    this->buffer = buffer;
    this->bufferPool = bufferPool;
}

CircularBuffer* RetransmissionTiming::getCircularBuffer()
{
    return buffer;
}

bool RetransmissionTiming::isTimerInitialized()
{
    return timerRetransmit.initialized();
}

bool RetransmissionTiming::startRTTMeasure(uint32_t seq)
{
    // We ensure that we are not already performing a measure at the moment
    if(measureInProgress)
        return false;

    measureInProgress = true;
    rttSeq = seq;
    // Save the current time to be able to compute the RTT when we receive the corresponding ACK
    measureStartTime.assign_now();

    return true;
}

bool RetransmissionTiming::signalAck(uint32_t ack)
{
    if(owner != NULL)
        owner->signalAck(ack);

    if(!measureInProgress)
        return false;

    if(SEQ_GT(ack, rttSeq))
    {
        // The ACK is greater than the sequence number used to start the measure
        // It means the destination received the data used to estimate
        // the RTT
        measureEndTime.assign_now();
        measureInProgress = false;

        // Compute the RTT
        measureEndTime -= measureStartTime;
        uint32_t rtt = measureEndTime.msecval();


        if(srtt == 0)
        {
            // First measure
            srtt = rtt;
            rttvar = rtt / 2;
        }
        else
        {
            // Subsequent measures

            // We use float variables during the computations
            float rttvarFloat = (float)rttvar;
            float srttFloat = (float)srtt;
            float rttFloat = (float)rtt;

            float rttAbs = 0;
            if(srttFloat > rttFloat)
                rttAbs = srttFloat - rttFloat;
            else
                rttAbs = rttFloat - srttFloat;

            rttvarFloat = (1.0 - BETA) * rttvarFloat + BETA * rttAbs;
            srttFloat = (1.0 - ALPHA) * srttFloat + ALPHA * rttFloat;

            // We do not need a submillisecond precision so we can use integer
            // values
            rttvar = (uint32_t) rttvarFloat;
            srtt = (uint32_t) srttFloat;
        }

        // Computing the RTO
        rto = srtt;
        uint32_t rttvarFactor = K * rttvar;
        if(clockGranularity > rttvarFactor)
            rto += clockGranularity;
        else
            rto += rttvarFactor;

        checkRTOMinValue();
        checkRTOMaxValue();

        return true;
    }

    return false;
}

bool RetransmissionTiming::signalRetransmission(uint32_t expectedAck)
{
    if(!measureInProgress)
        return false;

    // If we retransmit data with an expected ACK greater than the sequence
    // number we use for the measure, it means we are retransmitting the data
    // used for the measure. Thus, it can't be used to estimate the RTT
    // (Karn's algorithm)
    if(SEQ_GT(expectedAck, rttSeq))
    {
        // Stop the measure
        measureInProgress = false;

        return true;
    }

    return false;
}

bool RetransmissionTiming::isMeasureInProgress()
{
    return measureInProgress;
}

bool RetransmissionTiming::startTimer()
{
    if(!isTimerInitialized() || isTimerRunning())
        return false;

    timerRetransmit.schedule_after_msec(rto);

    return true;
}

bool RetransmissionTiming::startTimerDoubleRTO()
{
    if(!isTimerInitialized() || isTimerRunning())
        return false;

    rto *= 2;
    checkRTOMaxValue();
    timerRetransmit.schedule_after_msec(rto);

    return true;
}

bool RetransmissionTiming::stopTimer()
{
    if(!isTimerInitialized() || !isTimerRunning())
        return false;

    timerRetransmit.unschedule();

    return true;
}

bool RetransmissionTiming::restartTimer()
{
    stopTimer();

    if(!startTimer())
        return false;

    return true;
}

bool RetransmissionTiming::fireNow()
{
    if(!isTimerInitialized())
        return false;

    timerRetransmit.schedule_now();

    return true;
}

bool RetransmissionTiming::sendMoreData()
{
    if(!isTimerInitialized())
        return false;

    timerThread.schedule_now();

    return true;
}

bool RetransmissionTiming::isTimerRunning()
{
    if(!isTimerInitialized())
        return false;

    if(!timerRetransmit.scheduled())
        return false;

    return timerRetransmit.expiry() >= Timestamp::now();
}

void RetransmissionTiming::checkRTOMaxValue()
{
    // Max 60 seconds for the RTO
    if(rto > 60000)
        rto = 60000;
}

void RetransmissionTiming::checkRTOMinValue()
{
    // RTO should be at least one second (RFC 1122)
    if(rto < 1000)
        rto = 1000;
}

void RetransmissionTiming::timerRetransmitFired(Timer *timer, void *data)
{
    //struct fcb *fcb = (struct fcb*)((struct retransmissionTimerData*)data)->fcb;
    TCPRetransmitter *retransmitter =
        (TCPRetransmitter*)((struct retransmissionTimerData*)data)->retransmitter;
    retransmitter->retransmissionTimerFired();
}

void RetransmissionTiming::timerThreadFired(Timer *timer, void *data)
{
    //struct fcb *fcb = (struct fcb*)((struct retransmissionTimerData*)data)->fcb;
    TCPRetransmitter *retransmitter =
        (TCPRetransmitter*)((struct retransmissionTimerData*)data)->retransmitter;
    retransmitter->transmitMoreData();
}

bool RetransmissionTiming::isManualTransmissionDone()
{
    return manualTransmissionDone;
}

uint32_t RetransmissionTiming::getLastManualTransmission()
{
    return lastManualTransmission;
}

void RetransmissionTiming::setLastManualTransmission(uint32_t lastManualTransmission)
{
    this->lastManualTransmission = lastManualTransmission;
    manualTransmissionDone = true;
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(false)
ELEMENT_PROVIDES(RetransmissionTiming)
