// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_REPLAY_HH
#define CLICK_REPLAY_HH
#include <click/batchelement.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/vector.hh>
#include <click/notifier.hh>
CLICK_DECLS

/*
=c

TimeRange(I<keyword SIMPLE>)

=s timestamps

monitor range of packet timestamps

=d

TimeRange passes packets along unchanged, monitoring the smallest range that
contains all of their timestamps. You can access that range with handlers.

Keyword arguments are:

=over 8

=item SIMPLE

Boolean. If true, then packets arrive at TimeRange with monotonically
increasing timestamps. Default is false.

=back

=h first read-only

Returns the earliest timestamp observed, or "0.0" if no packets have passed.

=h last read-only

Returns the latest timestamp observed, or "0.0" if no packets have passed.

=h range read-only

Returns the earliest and latest timestamps observed, separated by a space.

=h interval read-only

Returns the difference between the earliest and latest timestamps observed,
in seconds.

=h reset write-only

Clears the stored range. Future packets will accumulate a new range.

=a

TimeFilter */

class Replay : public BatchElement { public:

	Replay() CLICK_COLD;
    ~Replay() CLICK_COLD;

    const char *class_name() const	{ return "Replay"; }
    const char *port_count() const	{ return "1-/="; }
    const char *flow_code() const	{ return "#/#"; }
    const char *processing() const	{ return PULL; }

    bool get_runnable_thread(Bitvector&) {
    	return false;
    }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    void* cast(const char *n);
    int initialize(ErrorHandler *errh);

    Packet* pull(int port);
    PacketBatch* pull_batch(int port, unsigned max);

    void cleanup(CleanupStage);

    bool run_task(Task*);

  private:

    bool _active;
    bool _loaded;

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

};

CLICK_ENDDECLS
#endif
