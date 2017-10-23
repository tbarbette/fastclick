#ifndef CLICK_FLOWIPLOADBALANCER_HH
#define CLICK_FLOWIPLOADBALANCER_HH
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
 * 3-tuple, IP pair and original or new port
 */
struct TTuple {
    IPPair pair;
    uint16_t port;
    TTuple(IPPair _pair, uint16_t _port) : pair(_pair),port(_port) {
    }
};

struct LBEntry {
    IPAddress chosen_server;
    uint16_t port;
    LBEntry(IPAddress addr, uint16_t port) : chosen_server(addr), port(port) {

    }
    inline hashcode_t hashcode() const {
       return CLICK_NAME(hashcode)(chosen_server) + CLICK_NAME(hashcode)(port);
   }

   inline bool operator==(LBEntry other) const {
       return (other.chosen_server == chosen_server && other.port == port);
   }

};
#if IPLOADBALANCER_MP
typedef HashTableMP<LBEntry,TTuple> LBHashtable;
#else
typedef HashTable<LBEntry,TTuple> LBHashtable;
#endif
class FlowIPLoadBalancer : public FlowSpaceElement<TTuple> {

public:

    FlowIPLoadBalancer() CLICK_COLD;
    ~FlowIPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, TTuple*, PacketBatch *);
private:
    struct state {
        int last;
        uint16_t min_port;
        uint16_t max_port;
        Vector<uint16_t> ports;
    };
    per_thread<state> _state;
    Vector<IPAddress> _dsts;
    Vector<IPAddress> _sips;


    LBHashtable _map;
    friend class FlowIPLoadBalancerReverse;
};

class FlowIPLoadBalancerReverse : public FlowSpaceElement<TTuple> {

public:

    FlowIPLoadBalancerReverse() CLICK_COLD;
    ~FlowIPLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "FlowIPLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }

    FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, TTuple*, PacketBatch *);
private:
    FlowIPLoadBalancer* _lb;
};


CLICK_ENDDECLS
#endif
