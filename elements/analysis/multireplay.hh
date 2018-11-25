// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_MULTIREPLAY_HH
#define CLICK_MULTIREPLAY_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
#include "replay.hh"
CLICK_DECLS

class MultiReplay : public ReplayBase { public:

	MultiReplay() CLICK_COLD;
    ~MultiReplay() CLICK_COLD;

    const char *class_name() const	{ return "MultiReplay"; }
    const char *flow_code() const	{ return "#/#"; }
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

    Vector<struct s_output> _output;


};

class MultiReplayUnqueue : public ReplayBase { public:
	MultiReplayUnqueue() CLICK_COLD;
    ~MultiReplayUnqueue() CLICK_COLD;

    const char *class_name() const	{ return "MultiReplayUnqueue"; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL_TO_PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    bool get_spawning_threads(Bitvector& bmp, bool, int) override {
        bmp[router()->home_thread_id(this)] = true;
        return false;
    }

    bool run_task(Task*);

};

CLICK_ENDDECLS
#endif
