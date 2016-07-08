#include <click/config.h>
#include <click/glue.hh>
#include "retransmissiontiming.hh"

CLICK_DECLS

RetransmissionTiming::RetransmissionTiming()
{
    computeClockGranularity();
    srtt = 0;
    rttvar = 0;
    rto = 3000; // RFC 1122
    measureInProgress = false;
}

RetransmissionTiming::~RetransmissionTiming()
{

}

void RetransmissionTiming::computeClockGranularity()
{
    // Determine the smallest amount of time measurable for the RTT values

    // We first check the epsilon of the timestamps
    uint32_t epsilon = Timestamp::epsilon().msec();

    // We then check the precision of the timers
    uint32_t timerAdjustment = timer.adjustment().msec();

    // We select the highest value between both of them
    if(epsilon < timerAdjustment)
        clockGranularity = timerAdjustment;
    else
        clockGranularity = epsilon;

}

void RetransmissionTiming::initTimer(struct fcb* fcb, TCPRetransmitter *retransmitter, TimerCallback f)
{
    timer.initialize((Element*)retransmitter);
    // Assign the callback of the timer
    // Give it a pointer to the fcb so that when the timer fires, we
    // can access it
    timer.assign(f, (void*)fcb);
}

bool RetransmissionTiming::timerInitialized()
{
    return timer.initialized();
}
/*
bool RetransmissionTiming::startRTTMeasure()
{
    if(measureInProgress)
        return false;

    measureInProgress = true;
    measureStartTime.assign_now();

    return true;
}

bool RetransmissionTiming::endRTTMeasure()
{
    if(!measureInProgress)
        return false;

    measureEndTime.assign_now();

    measureInProgress = false;

    return true;
}

bool RetransmissionTiming::cancelMeasure()
{
    if(!measureInProgress)
        return false;

    measureInProgress = true;

    return true;
}
*/
bool RetransmissionTiming::isMeasureInProgress()
{
    return measureInProgress;
}

CLICK_ENDDECLS

ELEMENT_PROVIDES(RetransmissionTiming)
