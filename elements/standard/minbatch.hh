#ifndef CLICK_MINBATCH_HH
#define CLICK_MINBATCH_HH

#include <click/string.hh>
#include <click/timer.hh>
#include <click/batchelement.hh>
#include <vector>

CLICK_DECLS

/**
 * Ensure that patch going through have a minimal size, if not
 * it waits for more packet, using a timeout to limit the amount of waiting time.
 */
class MinBatch: public BatchElement { public:

    MinBatch() CLICK_COLD;

    const char *class_name() const override		{ return "MinBatch"; }
    const char *port_count() const override		{ return "1/1"; }
    const char *processing() const override		{ return PUSH; }

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
