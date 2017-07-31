#ifndef CLICK_MINBATCH_HH
#define CLICK_MINBATCH_HH

#include <click/string.hh>
#include <click/timer.hh>
#include <click/batchelement.hh>
#include <vector>

CLICK_DECLS
class MinBatch: public BatchElement { public:

    MinBatch() CLICK_COLD;

    const char *class_name() const		{ return "MinBatch"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    bool can_live_reconfigure() const		{ return false; }

    bool run_task(Task *task);

    void push_batch(int port, PacketBatch *p) override;

 private:

    class State {
    public:
        State() : last_batch(0), timers(0) {};

        PacketBatch* last_batch;
        Timer*  timers;
    };

    per_thread<State> _state;

    unsigned _burst;
    int _timeout;

};
#endif
CLICK_ENDDECLS
