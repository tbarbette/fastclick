// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_REPLAY_HH
#define CLICK_REPLAY_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
#include <click/tinyexpr.hh>
#include <click/error.hh>
#include <strings.h>
CLICK_DECLS

class Args;

class ReplayBase : public BatchElement { public:
	ReplayBase() CLICK_COLD;
    ~ReplayBase() CLICK_COLD;

    const char *port_count() const override	{ return "1-/="; }

    int parse(Args*);

    void cleanup(CleanupStage);
protected:
    inline bool load_packets();
    void cleanup_packets();
    inline void check_end_loop(Task* t, bool force_time);
    static int write_handler(const String &, Element *e, void *thunk, ErrorHandler *errh);
    void add_handlers() override;
    void set_active(bool active);

    void reset_time();

    struct s_input {
		NotifierSignal signal;
    };
    Vector<struct s_input> _input;

    bool _active;
    bool _loaded;
    unsigned int _burst;
    int _stop;
    int _stop_time;
    bool _quick_clone;
    Task _task;
    int _limit;

    Packet* _queue_head;
    Packet* _queue_current;
    Timestamp _current;
    bool _use_signal;
    bool _verbose;
    bool _freeonterminate;
    Timestamp _timing_packet;
    Timestamp _timing_real;
    Timestamp _lastsent_packet;
    Timestamp _lastsent_real;
    Timestamp _startsent;
    TinyExpr _fnt_expr;
};

/*
=c

Replay([, I<KEYWORDS>])

=s traces

replay an input of packets at a given speed

=d

Preload packets in RAM, then replays them a certain number of time. This is a pull elements, see ReplayUnqueue for a pull-to-push version.

Keyword arguments are:

=over 8

=item STOP
Integer.  Number of loop to replay.

=item STOP_TIME
Integer. If > 0, also bound the number of replay loops using a time limit, in seconds.

=item QUICK_CLONE
Boolean. If true, the packets will be cloned using an internal DPDK reference counter, so this will avoid
the packets being duplicated by Click if they are modified and replayed more than once.
The downside is if the replay loop is too fast, the NIC might send corrupted packets.

=item BURST
Integer. Number of packets to send at once.

=item VERBOSE
Integer. Verbosity level.

=item FREEONTERMINATE
Boolean. Free packets on the last run.

=item LIMIT
Integer. Max number of packets to preload.

=item ACTIVE
Boolean. Wether this element should start in active mode. To be used with the active handler.

=item USE_SIGNAL
Boolean. If true, use an upstream empty signal to know wether the element should stop polling for packets when
preloading. Else, stops preloading packets when the pulling returns no packets. Default true.

=back

=e

  FromDump(file.pcap, ) -> ReplayUnqueue(3, QUEUE 1) -> ...
  
=h device read-only

=a ReplayUnqueue, MultiReplayUnqueue

*/
class Replay : public ReplayBase { public:

	Replay() CLICK_COLD;
    ~Replay() CLICK_COLD;

    const char *class_name() const override	{ return "Replay"; }
    const char *port_count() const override  { return "1-/="; }
    const char *flow_code() const override   { return "#/#"; }
    const char *processing() const override	{ return PULL; }

    bool get_spawning_threads(Bitvector&, bool, int) override {
        return false;
    }

    void* cast(const char *n) CLICK_COLD;
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    Packet* pull(int port);
#if HAVE_BATCH
    PacketBatch* pull_batch(int port, unsigned max);
#endif

    bool run_task(Task*);

  private:
    unsigned int _queue;
    ActiveNotifier _notifier;

    struct s_output {
	SPSCDynamicRing<Packet*> ring;
    };

    struct s_output _output;
};

/*
=c

ReplayUnqueue([, I<KEYWORDS>])

=s traces

replay an input of packets at a given speed, pull to push

=d

Technically equivalent to Replay->Unqueue-> it is more efficient.

Keyword arguments are the same than Replay, with the addition of:

=over 8

=item TIMING
Integer. If 0, replays packets as fast as possible. If >0, give an acceleration speed of the
original timing of the packet.

=item TIMING_FNT
String.  A function that can be used to change the TIMING according to 
the current time. The parsing uses TinyFNT and therefore follows the format.
The variable containing the time is x. E.g. "10 + min(90,10*x)" will have an
acceleration from 10 to 100% in 9 seconds. Note that if the function goes to 0, the element stops.
See the Metron (NSDI'18) paper for examples. Supports @1 and @2 for the predifined functions for that paper. 
@1 is equivalent to "100 * ((sin(-pi/2 + (x/10)^2.5) * (-x/"+time+" + 1) + 1) * (("+max+" - 1) / 2) + 1)"
where TIME is STOP_TIME argument and MAX is the value of TIMING given above.
@2 is equivalent to "100 * ((-squarewave(((x + 40) * 1/50) ^ 5) * (-x / "+time+" + 1) + 1) * (("+max+" - 1) / 2) + 1)"
Ineffective if TIMING is not true. Defaults to an empty string (inactive).

*/
class ReplayUnqueue : public ReplayBase { public:
	ReplayUnqueue() CLICK_COLD;
    ~ReplayUnqueue() CLICK_COLD;

    const char *class_name() const override	{ return "ReplayUnqueue"; }
    const char *flow_code() const override	{ return "#/#"; }
    const char *processing() const override	{ return PULL_TO_PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    bool get_spawning_threads(Bitvector& bmp, bool, int) override {
        bmp[router()->home_thread_id(this)] = true;
        return false;
    }

    bool run_task(Task*);


    void add_handlers() override;

private:

    unsigned _timing;

};

inline bool ReplayBase::load_packets() {
        Packet* p_input[ninputs()];
        bzero(p_input,sizeof(Packet*) * ninputs());
        int first_i = -1;
        Timestamp first_t;
        Packet* queue_tail = 0;
        int count = 0;

        click_chatter("Loading %s with %d inputs.",name().c_str(),ninputs());
        //Dry is the index of the first input to dry out
        int dry = -1;
        do {
            for (int i = 0; i < ninputs(); i++) {
                if (p_input[i] == 0) {
                    do_pull:
#if HAVE_BATCH
                    if (likely(receives_batch))
                        p_input[i] = input_pull_batch(i,1)->first();
                    else
                        p_input[i] = PacketBatch::make_from_packet(input(i).pull())->first();
#else
                        p_input[i] = input(i).pull();
#endif
                    if (p_input[i] == 0) {
                        if (_use_signal && _input[i].signal.active()) {
                            goto do_pull;
                        }
                        dry = i;
                        break;
                    }
                }

                Packet*& p = p_input[i];
                Timestamp t = p->timestamp_anno();
                if (i == 0) {
                    first_i = 0;
                    first_t = t;
                } else {
                    if (t <  first_t) {
                        first_i = i;
                        first_t = t;
                    }
                }
            }
            if (dry >= 0)
                break;
            if (!_queue_head) {
                _queue_head = p_input[first_i];
            } else {
                queue_tail->set_next(p_input[first_i]);
            }
            queue_tail = p_input[first_i];
            SET_PAINT_ANNO(p_input[first_i],first_i);
            p_input[first_i] = 0;
            count++;
            if (!router()->running())
                return false;
        } while(dry < 0 && (_limit < 0 || count < _limit));

        click_chatter("%s : Successfully loaded %d packets. Input %d dried out.",name().c_str(),count,dry);

        //Clean left overs
        for (int i = 0; i < ninputs(); i++) {
            if (p_input[i])
                p_input[i]->kill();
        }
        _loaded = true;
        _queue_current = _queue_head;
        reset_time();
        return true;
}

inline void ReplayBase::check_end_loop(Task* t, bool force_time) {
    if (unlikely(!_queue_current)) {
        _queue_current = _queue_head;
        reset_time();
        if (_stop_time == 0) {
            if (_verbose)
                click_chatter("%p{element}: Replay loop (%d loops left)", this, _stop);
            if (_stop > 0)
                _stop--;
            if (_stop == 0 && _verbose)
                click_chatter("%p{element}: stopped because there is 0 loops left", this);
        } else {
            int diff = (Timestamp::now_steady() - _startsent).msecval() / 1000;
            if (diff >= _stop_time) {
                if (_verbose)
                    click_chatter("Replay stopped after %d seconds (%d loops left)",diff,_stop);
                _stop = 0;
            } else {
                if (_verbose)
                    click_chatter("%p{element}: continue after %d/%d seconds (%d loops left)", this, diff, _stop_time, _stop);
                if (_stop > 0)
                    _stop--;
                if (_stop == 0 && _verbose)
                    click_chatter("%p{element}: stopped early after %d/%d seconds because there is 0 loops left", this, diff, _stop_time);
            }

        }
        if (_stop == 0) {
stop:
            router()->please_stop_driver();
            _active = false;
            _startsent = Timestamp::uninitialized_t();
            return;
        }

    } else if (unlikely(_stop_time > 0 && force_time)) {
         int diff = (Timestamp::now_steady() - _startsent).msecval() / 1000;
         if (diff >= _stop_time)
             goto stop;
    }
    t->fast_reschedule();
}

CLICK_ENDDECLS
#endif
