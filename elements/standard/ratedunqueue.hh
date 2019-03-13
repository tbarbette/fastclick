// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RATEDUNQUEUE_HH
#define CLICK_RATEDUNQUEUE_HH
#include <click/batchelement.hh>
#if HAVE_INT64_TYPES
# include <click/tokenbucket64.hh>
#else
# include <click/tokenbucket.hh>
#endif
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
CLICK_DECLS

#if HAVE_INT64_TYPES
    class Bandwidth64Arg;
#else
    class BandwidthArg;
#endif

/*
 * =c
 * RatedUnqueue(RATE, I[<KEYWORDS>])
 * =s shaping
 * pull-to-push converter
 * =d
 *
 * Pulls packets at the given RATE in packets per second, and pushes them out
 * its single output.  It is implemented with a token bucket.  The capacity of
 * this token bucket defaults to 20 milliseconds worth of tokens, but can be
 * customized by setting BURST_DURATION or BURST_SIZE.
 *
 * Keyword arguments are:
 *
 * =over 8
 *
 * =item RATE
 *
 * Integer.  Token bucket fill rate in packets per second.
 *
 * =item BURST_DURATION
 *
 * Time.  If specified, the capacity of the token bucket is calculated as
 * rate * burst_duration.
 *
 * =item BURST_SIZE
 *
 * Integer.  If specified, the capacity of the token bucket is set to this
 * value.
 *
 * =h rate read/write
 *
 * =a BandwidthRatedUnqueue, Unqueue, Shaper, RatedSplitter */

class RatedUnqueue : public BatchElement { public:

    RatedUnqueue() CLICK_COLD;

    const char *class_name() const	{ return "RatedUnqueue"; }
    const char *port_count() const	{ return PORTS_1_1; }
    const char *processing() const	{ return PULL_TO_PUSH; }
    bool is_bandwidth() const		{ return class_name()[0] == 'B'; }

#if HAVE_INT64_TYPES
    typedef uint64_t ucounter_t;
    typedef int64_t counter_t;
    typedef TokenBucket64 token_bucket_t;
    typedef Bandwidth64Arg bandwidth_arg_t;
#else
    typedef uint32_t ucounter_t;
    typedef int32_t counter_t;
    typedef TokenBucket token_bucket_t;
    typedef BandwidthArg bandwidth_arg_t;
#endif

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    static int configure_helper(token_bucket_t *tb, bool is_bandwidth, Element *elt, Vector<String> &conf, ErrorHandler *errh);
    enum { tb_bandwidth_thresh = 131072 };

    bool can_live_reconfigure() const	{ return true; }
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    bool run_task(Task *);

  protected:

    token_bucket_t _tb;
    Task _task;
    Timer _timer;
    NotifierSignal _signal;
    ucounter_t _runs;
    ucounter_t _packets;
    ucounter_t _pushes;
    ucounter_t _failed_pulls;
    ucounter_t _empty_runs;

    unsigned _burst;

    enum { h_calls, h_rate };

    static String read_handler(Element *e, void *thunk) CLICK_COLD;

    bool _active;
};

CLICK_ENDDECLS
#endif
