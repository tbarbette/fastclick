#ifndef CLICK_FLOWCOUNTER_HH
#define CLICK_FLOWCOUNTER_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/flow/flowelement.hh>

CLICK_DECLS


/*
=c

FlowCounter([CLOSECONNECTION])

=s flow

Counts all flows passing by, the number of active flows, and the number of 
packets per flow.

 */


class FlowCounter : public FlowStateElement<FlowCounter,int>
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
        if (_state->lengths.size() < *fcb) {
            _state->lengths.resize(*fcb, 0);
        }
        _state->lengths[*fcb - 1]++;
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
        Vector<int> lengths;
    };
    per_thread<fcstate> _state;

    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
