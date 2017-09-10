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

struct PortRef {
    PortRef(uint16_t _port) : port(_port), closing(false) {
        ref = 0;
    }
    uint16_t port;
    atomic_uint32_t ref;
    bool closing;
};


struct NATEntryOUT {
    IPPort map;
    PortRef* ref;
    bool fin_seen;
};

struct NATEntryIN {
    PortRef* ref;
    bool fin_seen;
};

typedef HashTableMP<uint16_t,NATEntryOUT> NATHashtable;

#define NAT_FLOW_TIMEOUT 60 * 1000

class FlowIPNAT : public FlowStateElement<FlowIPNAT,NATEntryIN> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNAT"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL(TCP_MIDDLEBOX,UDP_MIDDLEBOX);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh);

    static const int timeout = NAT_FLOW_TIMEOUT;
    PortRef* pick_port();
    bool new_flow(NATEntryIN*, Packet*);
    void release_flow(NATEntryIN*);

    void push_batch(int, NATEntryIN*, PacketBatch *);
private:
    struct state {
        DynamicRing<PortRef*> available_ports;
    };
    per_thread<state> _state;

private:
    IPAddress _sip;
    NATHashtable _map;
    friend class FlowIPNATReverse;
};



class FlowIPNATReverse : public FlowStateElement<FlowIPNATReverse,NATEntryOUT> {

public:

    FlowIPNATReverse() CLICK_COLD;
    ~FlowIPNATReverse() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNATReverse"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT_DUAL(TCP_MIDDLEBOX,UDP_MIDDLEBOX);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    static const int timeout = NAT_FLOW_TIMEOUT;
    bool new_flow(NATEntryOUT*, Packet*);
    void release_flow(NATEntryOUT*);

    void push_batch(int, NATEntryOUT*, PacketBatch *);
private:

    FlowIPNAT* _in;
};

CLICK_ENDDECLS
#endif
