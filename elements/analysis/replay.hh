// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_REPLAY_HH
#define CLICK_REPLAY_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
#include <strings.h>
CLICK_DECLS

class Args;

class ReplayBase : public BatchElement { public:
	ReplayBase() CLICK_COLD;
    ~ReplayBase() CLICK_COLD;

    const char *port_count() const	{ return "1-/="; }

    int parse(Args*);

    void cleanup(CleanupStage);
protected:
    inline bool load_packets();
    void cleanup_packets();
    inline void check_end_loop(Task* t);
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
    bool _quick_clone;
    Task _task;
    int _limit;

    Packet* _queue_head;
    Packet* _queue_current;
    Timestamp _current;
    bool _use_signal;
    bool _verbose;
    bool _freeonterminate;
    Timestamp _lastsent_p;
    Timestamp _lastsent_real;
};


class Replay : public ReplayBase { public:

	Replay() CLICK_COLD;
    ~Replay() CLICK_COLD;

    const char *class_name() const	{ return "Replay"; }
    const char *port_count() const  { return "1-/="; }
    const char *flow_code() const   { return "#/#"; }
    const char *processing() const	{ return PULL; }

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
    	DynamicRing<Packet*> ring;
    };

    struct s_output _output;
};

class ReplayUnqueue : public ReplayBase { public:
	ReplayUnqueue() CLICK_COLD;
    ~ReplayUnqueue() CLICK_COLD;

    const char *class_name() const	{ return "ReplayUnqueue"; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL_TO_PUSH; }

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
                    p_input[i] = input_pull_batch(i,1);
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

inline void ReplayBase::check_end_loop(Task* t) {
    if (unlikely(!_queue_current)) {
        _queue_current = _queue_head;
        reset_time();
        if (_stop > 0)
            _stop--;
        if (_stop == 0) {
            router()->please_stop_driver();
            _active = false;
            return;
        }
        if (_verbose)
            click_chatter("Replay loop");
    }
    t->fast_reschedule();
}

CLICK_ENDDECLS
#endif
