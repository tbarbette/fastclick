#ifndef CLICK_FLOWCOUNTER_HH
#define CLICK_FLOWCOUNTER_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/statvector.hh>
#include <click/flow/flowelement.hh>

CLICK_DECLS


/*
=c

FlowCounter([CLOSECONNECTION])

=s flow

Counts the number of flows and packets per flow

=d

This element uses the flow subsystem to count the number of flows passing by,
the one considered still active (using the upstream FlowManager's definition of
active flow) and the number of packets per flow.


=h count

Returns the number of flows seen

=h open

Returns the number of flows currently active

=h average

Returns the average length of a flow

=h median

Returns the median length of flows

=h dump

Print the histogram of flow sizes


=a MidStat

 */


class FlowCounter : public FlowStateElement<FlowCounter,int>, StatVector<int>
{
public:
    /** @brief Construct an FlowCounter element
     */
    FlowCounter() CLICK_COLD;

    const char *class_name() const override        { return "FlowCounter"; }
    const char *port_count() const override        { return PORTS_1_1; }
    const char *processing() const override        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void release_flow(int* fcb) {
        _state->open--;
        unsigned n = *fcb - 1;
        if (n > 32767)
            n = 32767;
        (*stats)[n]++;
    }

    const static int timeout = 15000;

    void push_flow(int port, int* fcb, PacketBatch*);

    inline bool new_flow(void*, Packet*) {
        _state->count++;
        _state->open++;
        return true;
    }

    void add_handlers() override CLICK_COLD;
protected:

    struct fcstate {
        long count;
        long open;
    };
    per_thread<fcstate> _state;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
