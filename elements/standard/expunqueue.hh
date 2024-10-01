// -*- c-basic-offset: 4 -*-
#ifndef CLICK_EXPUnqueue_HH
#define CLICK_EXPUnqueue_HH
#include <click/batchelement.hh>
#include <click/tokenbucket.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include <random>

CLICK_DECLS

/*
 * =c
 * EXPUnqueue(RATE, I[<KEYWORDS>])
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
 * =a BandwidthEXPUnqueue, Unqueue, Shaper, RatedSplitter */

class EXPUnqueue : public BatchElement { public:

    EXPUnqueue() CLICK_COLD;

    const char *class_name() const override	{ return "EXPUnqueue"; }
    const char *port_count() const override	{ return PORTS_1_1; }
    const char *processing() const override	{ return PULL_TO_PUSH; }
    bool is_bandwidth() const		{ return class_name()[0] == 'B'; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int configure_helper(bool is_bandwidth, Element *elt, Vector<String> &conf, ErrorHandler *errh);
    enum { tb_bandwidth_thresh = 131072 };

    bool can_live_reconfigure() const	{ return true; }
    int initialize(ErrorHandler *) CLICK_COLD;
    void add_handlers() CLICK_COLD;

    bool run_task(Task *);

  protected:

  double ran_expo(double lambda);
    static int write_param(const String &conf, Element *e, void *user_data,
		     ErrorHandler *errh);

    std::mt19937 _gen;
    std::exponential_distribution<> _poisson;
    uint64_t _bucket;
    Task _task;
    Timer _timer;
    NotifierSignal _signal;
    uint32_t _runs;
    uint32_t _packets;
    uint32_t _pushes;
    uint32_t _failed_pulls;
    uint32_t _empty_runs;
    uint32_t _burst;
    double _lambda;
    uint64_t _inter_arrival_time;
    uint64_t _last;
    void refill(uint64_t now);

    enum { h_calls, h_rate };

    static String read_handler(Element *e, void *thunk) CLICK_COLD;

    bool _active;
};

CLICK_ENDDECLS
#endif
