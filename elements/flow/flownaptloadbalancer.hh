#ifndef CLICK_FLOWNAPTLOADBALANCER_HH
#define CLICK_FLOWNAPTLOADBALANCER_HH
#include <click/config.h>
#include <click/tcphelper.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/vector.hh>

#include <click/flow/flowelement.hh>
#include "flowipnat.hh"


#define LB_FLOW_TIMEOUT 60 * 1000

#define IPLOADBALANCER_MP 1
#if IPLOADBALANCER_MP
#include <click/hashtablemp.hh>
#else
#include <click/hashtable.hh>
#endif
CLICK_DECLS

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


/**
 * 3-tuple, IP pair and new port (or reference to it)
 */
struct TTuple {
    IPPair pair;
#if !HAVE_NAT_NEVER_REUSE
    uint16_t port;
    TTuple(IPPair _pair, uint16_t _port) : pair(_pair), port(_port) {
    }


    uint16_t get_port() {
        return port;
    }
#else
    NATCommon* ref;
    bool fin_seen;
    TTuple(IPPair _pair, NATCommon* _ref) : pair(_pair), ref(_ref), fin_seen(false) {
    }

    uint16_t get_port() {
        return ref->port;
    }
#endif
};


/**
 * 3-tuple, IP pair, original port and reference to the rewritten
 */
struct LBEntryOut : public TTuple { private:

    uint16_t _osport;
public:
    const IPAddress& get_original_sip() const {
        return pair.src;
    }
    const uint16_t& get_original_sport() const {
        return _osport;
    }

    LBEntryOut(IPPair _pair, uint16_t _port, NATCommon* _ref) : TTuple(_pair, _ref), _osport(_port) {
    }
};




#if IPLOADBALANCER_MP
typedef HashTableMP<LBEntry,LBEntryOut> LBHashtable;
#else
typedef HashTable<LBEntry,LBEntryOut> LBHashtable;
#endif

/**
 * Full-fledge NATing Load Balancer, with its own TCP state support (but it can be parially disabled if it's used in conjuction with other flow/context element that ensure state)
 */
class FlowNAPTLoadBalancer : public FlowStateElement<FlowNAPTLoadBalancer,TTuple>, public TCPHelper {

public:

    FlowNAPTLoadBalancer() CLICK_COLD;
    ~FlowNAPTLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "FlowNAPTLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);
    //FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 22/0/ffff 20/0/ffff:ARRAY", FLOW_TCP);

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    static const int timeout = LB_FLOW_TIMEOUT;
    bool new_flow(TTuple*, Packet*);
    void release_flow(TTuple*);

    void push_flow(int, TTuple*, PacketBatch *);

    static String read_handler(Element* e, void* thunk) CLICK_COLD;
    void add_handlers() override CLICK_COLD;

private:
    struct state {
        state() : last(0)
#if NAT_STATS
                  , conn(0), open(0)
#endif
        {
        }
        int last;

#if !HAVE_NAT_NEVER_REUSE
        uint16_t min_port;
        uint16_t max_port;
        Vector<uint16_t> ports;
#else
        MPSCDynamicRing<NATCommon*> available_ports;
#endif
#if NAT_STATS
        unsigned long long conn;
        unsigned long long open;
#endif
    };
    per_thread<state> _state;
    Vector<IPAddress> _dsts;
    Vector<IPAddress> _sips;
    bool _own_state;
    bool _accept_nonsyn;

    NATCommon* pick_port();

    LBHashtable _map;
    friend class FlowNAPTLoadBalancerReverse;
};

class FlowNAPTLoadBalancerReverse : public FlowStateElement<FlowNAPTLoadBalancerReverse, LBEntryOut>, public TCPHelper  {

public:

    FlowNAPTLoadBalancerReverse() CLICK_COLD;
    ~FlowNAPTLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "FlowNAPTLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }

    FLOW_ELEMENT_DEFINE_SESSION_DUAL(TCP_SESSION,UDP_SESSION);
    //FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffff 22/0/ffff:ARRAY", FLOW_TCP);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    static const int timeout = LB_FLOW_TIMEOUT;
    bool new_flow(LBEntryOut*, Packet*);
    void release_flow(LBEntryOut*);



    void push_flow(int, LBEntryOut*, PacketBatch *);
private:
    FlowNAPTLoadBalancer* _lb;
};


CLICK_ENDDECLS
#endif
