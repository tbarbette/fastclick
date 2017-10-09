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
    uint16_t port; //Port
    atomic_uint32_t ref; //Reference count
    bool closing; //Has one side started to close?
};


/**
 * NAT Entries for the mapping side : a mapping to the original port
 * and a flag to know if the mapping has been seen on this side.
 */
struct NATEntryIN {
    PortRef* ref;
    bool fin_seen;
};


/**
 * NAT Entries for the reverse side: the original IP and Port, a reference to
 * the port for reference counting, and a flagto know if the fin has been seen
 * on this side.
 */
struct NATEntryOUT {
    IPPort map;
    PortRef* ref;
    bool fin_seen;
};


typedef HashTableMP<uint16_t,NATEntryOUT> NATHashtable; //Table used to pass the mapping from the mapper to the reverse

#define NAT_FLOW_TIMEOUT 2 * 1000 //Flow timeout

/**
 * Efficient MiddleClick-based NAT
 *
 * Unlike rewriter, we use two separate elements instead of two ports, one for
 * the mapping side and another one (FlowIPNATReverse) for the reverse side.
 *
 * Port allocation is done using a FIFO queue of ports, per thread. We divide
 * the 1024-65530 range into then number of threads that will pass by.
 *
 * The mapper (this element) pass mappings to the reverse using a thread safe
 * hash-table when a new mapping is done. Afterwards the middleclick scratchpad
 * is set and the hash-table is never used again.
 * When reverse see a new flow, it looks for it in the hash-table and remove it,
 * and sets its scratchpad, then never use the hash-table again.
 *
 * Therefore both side only use their scratchpad for the rest of the flow, that
 * is classified once for all 4-tuples functions (TCP and UDP based).
 */
class FlowIPNAT : public FlowStateElement<FlowIPNAT,NATEntryIN> {

public:

    FlowIPNAT() CLICK_COLD;
    ~FlowIPNAT() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNAT"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    //UDP or TCP
    FLOW_ELEMENT_DEFINE_CONTEXT("9/06! 12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 22/0/ffff 20/0/ffff:ARRAY");

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


/**
 * See FlowIPNAT
 */
class FlowIPNATReverse : public FlowStateElement<FlowIPNATReverse,NATEntryOUT> {

public:

    FlowIPNATReverse() CLICK_COLD;
    ~FlowIPNATReverse() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPNATReverse"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    FLOW_ELEMENT_DEFINE_CONTEXT("9/06! 12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 20/0/ffff 22/0/ffff:ARRAY");

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
