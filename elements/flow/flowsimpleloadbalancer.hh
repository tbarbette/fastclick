#ifndef CLICK_FlowSimpleIPLoadBalancer_HH
#define CLICK_FlowSimpleIPLoadBalancer_HH
#include <click/config.h>
#include <click/flowelement.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/vector.hh>

#define IPLOADBALANCER_MP 1
#if IPLOADBALANCER_MP
#include <click/hashtablemp.hh>
#else
#include <click/hashtable.hh>
#endif
CLICK_DECLS

/**
 * Chosen server
 */
struct SMapInfo {
    int srv;
};


class FlowSimpleIPLoadBalancer : public FlowSpaceElement<SMapInfo> {

public:

    FlowSimpleIPLoadBalancer() CLICK_COLD;
    ~FlowSimpleIPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "FlowSimpleIPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    //FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);
    FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 22/0/ffff 20/0/ffff:ARRAY", FLOW_TCP);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, SMapInfo*, PacketBatch *);
private:
    struct state {
        int last;
    };
    per_thread<state> _state;
    Vector<IPAddress> _dsts;

    friend class FlowSimpleIPLoadBalancerReverse;
};

class FlowSimpleIPLoadBalancerReverse : public FlowElement {

public:

    FlowSimpleIPLoadBalancerReverse() CLICK_COLD;
    ~FlowSimpleIPLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "FlowSimpleIPLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }

    //FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);
    FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffff 22/0/ffff:ARRAY", FLOW_TCP);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, PacketBatch *);
private:
    IPAddress _ip;
};


CLICK_ENDDECLS
#endif
