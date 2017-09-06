#ifndef CLICK_FLOWIPNAT_HH
#define CLICK_FLOWIPNAT_HH
#include <click/config.h>
#include <click/flowelement.hh>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/deque.hh>
CLICK_DECLS

#include <click/hashtablemp.hh>

CLICK_DECLS

struct IPPort {
    IPAddress ip;
    uint16_t port;
    IPPort(IPAddress addr, uint16_t port) : ip(addr), port(port) {

    }
    inline hashcode_t hashcode() const {
       return CLICK_NAME(hashcode)(ip) + CLICK_NAME(hashcode)(port);
   }

   inline bool operator==(IPPort other) const {
       return (other.ip == ip && other.port == port);
   }
};

typedef HashTableMP<uint16_t,IPPort> NATHashtable;

class FlowIPNAT : public FlowSpaceElement<uint16_t> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNat"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL(TCP_MIDDLEBOX,UDP_MIDDLEBOX);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    void push_batch(int, uint16_t*, PacketBatch *);
private:
    struct state {
        Deque<uint16_t> available_ports;
    };
    per_thread<state> _state;

private:
    IPAddress _sip;
    NATHashtable _map;
    friend class FlowIPNATReverse;
};

class FlowIPNATReverse : public FlowSpaceElement<IPPort> {

public:

    FlowIPNATReverse() CLICK_COLD;
    ~FlowIPNATReverse() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNATReverse"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL(TCP_MIDDLEBOX,UDP_MIDDLEBOX);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int, IPPort*, PacketBatch *);
private:

    FlowIPNAT* _in;
};

CLICK_ENDDECLS
#endif
