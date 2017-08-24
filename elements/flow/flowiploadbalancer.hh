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
typedef HashTableMP<LBEntry,IPPair> LBHashtable;
#else
typedef HashTable<LBEntry,IPPair> LBHashtable;
#endif
class FlowIPLoadBalancer : public FlowBufferElement<IPPair> {

public:

    FlowIPLoadBalancer() CLICK_COLD;
    ~FlowIPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL("9/06! 12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff","9/11! 12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff");

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);
private:
    per_thread<int> _last;
    Vector<IPAddress> _dsts;
    Vector<IPAddress> _sips;
    //IPAddress _sip;

    LBHashtable _map;
    friend class FlowIPLoadBalancerReverse;
};

class FlowIPLoadBalancerReverse : public FlowBufferElement<IPPair> {

public:

    FlowIPLoadBalancerReverse() CLICK_COLD;
    ~FlowIPLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "FlowIPLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL("9/06! 12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff","9/11! 12/0/ffffffff 16/0/ffffffff 20/0/ffff 22/0/ffff");

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);
private:
    FlowIPLoadBalancer* _lb;
};


CLICK_ENDDECLS
#endif
