// -*- c-basic-offset: 4 -*-
#ifndef CLICK_BWRATEDUNQUEUE_HH
#define CLICK_BWRATEDUNQUEUE_HH
#include "elements/standard/ratedunqueue.hh"
CLICK_DECLS

/*
 * =c
 * BandwidthRatedUnqueue(RATE, I[<KEYWORDS>])
 * =s shaping
 * pull-to-push converter
 * =d
 *
 * Pulls packets at the given RATE, and pushes them out its single output.  This
 * rate is implemented using a token bucket.  The capacity of this token bucket
 * defaults to 20 milliseconds worth of tokens, but can be customized by setting
 * one of BURST_DURATION or BURST_SIZE.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item RATE
 *
 * Bandwidth.  Token bucket fill rate.
 *
 * =item BURST_DURATION
 *
 * Time.  If specified, the capacity of the token bucket is calculated as
 * rate * burst_duration.
 *
 * =item BURST_BYTES
 *
 * Integer.  If specified, the capacity of the token bucket is set to this
 * value in bytes.
 *
 * =item EXTRA_LENGTH
 *
 * Boolean. If true, then the length of the packet in the EXTRA_LENGTH 
 * annotation will be taken into account. Default to false.
 *
 * =item LINK_RATE
 *
 * Boolean. If true, the given RATE is considered as a link rate, in bits/s,
 * taking into account inter-frame, preamble and FCS for each packets. FCS
 * is assumed to be stripped (hence, 24 bytes are added per-packets).
 *
 * =h rate read/write
 *
 * =a RatedUnqueue, Unqueue, BandwidthShaper, BandwidthRatedSplitter */

class BandwidthRatedUnqueue : public RatedUnqueue { public:

    BandwidthRatedUnqueue() CLICK_COLD;

    const char *class_name() const override	{ return "BandwidthRatedUnqueue"; }

    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;

    bool run_task(Task *) override;

  private:
    bool _use_extra_length;
    bool _link_rate;
};

CLICK_ENDDECLS
#endif
