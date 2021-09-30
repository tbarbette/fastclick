#ifndef CLICK_AGGREGATEGROUP_HH
#define CLICK_AGGREGATEGROUP_HH

#include <click/string.hh>
#include <click/timer.hh>
#include <click/batchelement.hh>
#include <vector>

CLICK_DECLS

/**
 * Group packets in batches by aggregate annotation
 */
class AggregateGroup: public BatchElement {
	class State {
	public:
		State() : last_packet(0), timers(0) {};

		PacketBatch* last_packet;
		Timer*  timers;
	};

	per_thread<State> _state;

	int timeout;

public:

	AggregateGroup() CLICK_COLD;

    const char *class_name() const		{ return "AggregateGroup"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return false; }

    bool run_task(Task *task);

    void push_batch(int port, PacketBatch *p);

 private:


};
#endif
CLICK_ENDDECLS
