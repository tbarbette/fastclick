// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_REPLAY_HH
#define CLICK_REPLAY_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
CLICK_DECLS

class Replay : public BatchElement { public:

	Replay() CLICK_COLD;
    ~Replay() CLICK_COLD;

    const char *class_name() const	{ return "Replay"; }
    const char *port_count() const	{ return "1-/="; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL; }

    bool get_runnable_thread(Bitvector& bmp) {
        for (int i = 0; i < noutputs(); i++)
            if (output_is_push(i))
                bmp[router()->home_thread_id(this)] = true;
        return false;
    }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void* cast(const char *n);
    int initialize(ErrorHandler *errh);

    Packet* pull(int port);
#if HAVE_BATCH
    PacketBatch* pull_batch(int port, unsigned max);
#endif
    void cleanup(CleanupStage);

    bool run_task(Task*);

  private:

    bool _active;
    bool _loaded;

    unsigned int _queue;
    unsigned int _burst;
    int _stop;

    Task _task;
    ActiveNotifier _notifier;

    struct s_input {
    	NotifierSignal signal;
    };
    Vector<struct s_input> _input;
    struct s_output {
    	DynamicRing<Packet*> ring;
    };
    Vector<struct s_output> _output;


    Packet* _queue_head;
    Packet* _queue_current;
    Timestamp _current;

    bool _quick_clone;

};

CLICK_ENDDECLS
#endif
