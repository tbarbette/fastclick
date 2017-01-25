#ifndef CLICK_FLOWIPLOADBALANCER_HH
#define CLICK_FLOWIPLOADBALANCER_HH
#include <click/config.h>
#include <click/flowelement.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/hashtablemp.hh>
CLICK_DECLS

class IPPair {
  public:

    IPAddress src;
    IPAddress dst;
    IPPair(IPAddress a, IPAddress b) {
        src = a;
        dst = b;
    }

    inline hashcode_t hashcode() const {
       return CLICK_NAME(hashcode)(src) + CLICK_NAME(hashcode)(dst);
   }

   inline bool operator==(IPPair other) const {
       return (other.src == src && other.dst == dst);
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
typedef HashTableMP<LBEntry,IPPair> LBHashtable;
class FlowIPLoadBalancer : public FlowBufferElement<IPPair> {

public:

    FlowIPLoadBalancer() CLICK_COLD;
    ~FlowIPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);
private:
    per_thread<int> _last;
    Vector<IPAddress> _dsts;
    IPAddress _sip;

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

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, IPPair*, PacketBatch *);
private:
    FlowIPLoadBalancer* _lb;
};


CLICK_ENDDECLS
#endif
