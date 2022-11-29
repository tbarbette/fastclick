#ifndef CLICK_FlowRateLimiter_HH
#define CLICK_FlowRateLimiter_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/tokenbucket.hh>
#include <click/flow/flowelement.hh>

CLICK_DECLS


/*
=c

FlowRateLimiter()

=s flow

Counts the number of packets per flow and let packet through only if it has at least N packets.

 */
    
struct FRLState {
    TokenBucket tb;
};

class StateChecker { public:
    struct str  {
    };
    static inline bool seen(void* v, str*) {
        return ((FRLState*)v)->tb.time_point() == 0;
    }
    static inline void mark_seen(void* v, str*) {
    }
    static inline void release(void* v, str*) {
        ((FRLState*)v)->tb.set_time_point(0);
    }
};

class FlowRateLimiter : public FlowStateElement<FlowRateLimiter,FRLState,StateChecker>
{
public:
    /** @brief Construct an FlowRateLimiter element
     */
    FlowRateLimiter() CLICK_COLD;

    const char *class_name() const override        { return "FlowRateLimiter"; }
    const char *port_count() const override        { return PORTS_1_1; }
    const char *processing() const override        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int configure_helper(TokenBucket *tb, Element *elt, Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;

    void release_flow(FRLState* fcb) {

    }

    const static int timeout = 15000;

    void push_flow(int port, FRLState* fcb, PacketBatch*);

    inline bool new_flow(FRLState* state, Packet*) {
        state->tb = _tb;
        return true;
    }

    void add_handlers() override CLICK_COLD;

    TokenBucket _tb;

protected:

    struct State {
        uint64_t dropped;
    };
    per_thread<State> _state;
    static String read_handler(Element *, void *) CLICK_COLD;
    static int write_handler(const String &, Element *, void *, ErrorHandler *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
