// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_MultiReplay_HH
#define CLICK_MultiReplay_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
CLICK_DECLS

class MultiReplayBase : public BatchElement { public:
	MultiReplayBase() CLICK_COLD;
    ~MultiReplayBase() CLICK_COLD;

    const char *port_count() const	{ return "1-/="; }

    void cleanup(CleanupStage);
protected:
    inline bool load_packets();
    void cleanup_packets();
    inline void check_end_loop(Task* t);
    static int write_handler(const String &, Element *e, void *thunk, ErrorHandler *errh);
    void add_handlers();
    void set_active(bool active);

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

    Packet* _queue_head;
    Packet* _queue_current;
    Timestamp _current;
    bool _use_signal;
    bool _verbose;
};


class MultiReplay : public MultiReplayBase { public:

	MultiReplay() CLICK_COLD;
    ~MultiReplay() CLICK_COLD;

    const char *class_name() const	{ return "MultiReplay"; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL; }

    bool get_spawning_threads(Bitvector&, bool) override {
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

    Vector<struct s_output> _output;


};

class MultiReplayUnqueue : public MultiReplayBase { public:
	MultiReplayUnqueue() CLICK_COLD;
    ~MultiReplayUnqueue() CLICK_COLD;

    const char *class_name() const	{ return "MultiReplayUnqueue"; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL_TO_PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    bool get_spawning_threads(Bitvector& bmp, bool) override {
        for (int i = 0; i < noutputs(); i++)
           bmp[router()->home_thread_id(this)] = true;
        return false;
    }

    bool run_task(Task*);

};

CLICK_ENDDECLS
#endif
