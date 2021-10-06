#ifndef CLICK_CTXIPLoadBalancer_HH
#define CLICK_CTXIPLoadBalancer_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/flow/flowelement.hh>

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

/**
 * IP LoadBalancer that only change the destination address
 * The routing must ensure packets go back by the source
 *
 * It is a context element because another element must handle the
 * classification (eg 5 tuple) and maintainance on the state. But it does
 * not register for any state.
 *
 * Typically you want TCPStateIN -> CTXIPLoadBalancer
 *
 */
class CTXIPLoadBalancer : public FlowSpaceElement<SMapInfo> {

public:

    CTXIPLoadBalancer() CLICK_COLD;
    ~CTXIPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "CTXIPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_flow(int, SMapInfo*, PacketBatch *);
private:
    struct state {
        int last;
    };
    per_thread<state> _state;
    Vector<IPAddress> _dsts;

    friend class CTXIPLoadBalancerReverse;
};

class CTXIPLoadBalancerReverse : public FlowElement {

public:

    CTXIPLoadBalancerReverse() CLICK_COLD;
    ~CTXIPLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "CTXIPLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_flow(int, PacketBatch *);
private:
    IPAddress _ip;
};


CLICK_ENDDECLS
#endif
