#ifndef CLICK_FLOWIPNAT_HH
#define CLICK_FLOWIPNAT_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/deque.hh>
#include <click/tcphelper.hh>
#include <click/ring.hh>
#include <click/flow/flowelement.hh>
CLICK_DECLS

#define DEBUG_NAT 0

#define NAT_STATS 0

//If 0, it just uses ports in loop. Fine for slow rates, but not realistic
//If 1, prevent reusing ports while connections are active
#define HAVE_NAT_NEVER_REUSE 1 //Actually for the LB

#define NAT_COLLIDE 1  //1 to avoid port collisions, if 0 just reuse port numbers after they exhaust

#if DEBUG_NAT>=2
# define nat_debug_chatter(args...) click_chatter(args)
# define nat_info_chatter(args...) click_chatter(args)
# define lb_assert(args...) assert(args)
#elif DEBUG_NAT==1
# define nat_debug_chatter(args...)
# define nat_info_chatter(args...) click_chatter(args)
# define lb_assert(args...) assert(args)
#else
# define nat_debug_chatter(args...)
# define nat_info_chatter(args...)
# define lb_assert(args...)
#endif

#include <click/hashtablemp.hh>

CLICK_DECLS

#if HAVE_NAT_NEVER_REUSE
struct NATCommon {
    NATCommon(uint16_t _port, MPSCDynamicRing<NATCommon*>* _ring) :
                port(_port), closing(0), ring(_ring) {
        ref = 0;
    }
    uint16_t port; // Port
    atomic_uint32_t ref; // Reference count
    uint8_t closing; // Has one side started to close?
    MPSCDynamicRing<NATCommon*>* ring;
};
#endif

class NATState {
    public:
        NATState(NATCommon* _ref) : fin_seen(false), ref(_ref) {}
        bool fin_seen;
        NATCommon* ref;
};

/**
 * NAT Entries for the mapping side : a mapping to the original port
 * and a flag to know if the mapping has been seen on this side.
 */
class NATEntryIN : public NATState {
    public:
        NATEntryIN(NATCommon* _ref) : NATState(_ref) {}
};


/**
 * NAT Entries for the reverse side: the original IP and Port, a reference to
 * the port for reference counting, and a flagto know if the fin has been seen
 * on this side.
 */
class NATEntryOUT: public NATState {
    public:
        NATEntryOUT(IPPort _map, NATCommon* _ref) : map(_map), NATState(_ref) {}
        IPPort map;
};

// Table used to pass the mapping from the mapper to the reverse
typedef HashTableMP<uint16_t,NATEntryOUT> NATHashtable;

#define NAT_FLOW_TIMEOUT 2 * 1000 //Flow timeout

/**
 * Efficient FCB-based NAT
 *
 * Unlike rewriter, we use two separate elements instead of two ports, one for
 * the mapping side and another one (FlowIPNATReverse) for the reverse side.
 *
 * Port allocation is done using a FIFO queue of ports, per thread. We divide
 * the 1024-65530 range into then number of threads that will pass by. Nothing
 * prevents using multiple ips, different ranges, etc. It's just not done yet.
 *
 * The mapper (this element) pass mappings to the reverse using a thread safe
 * hash-table when a new mapping is done. Afterwards the FCB scratchpad
 * is set and the hash-table is never used again.
 * When reverse see a new flow, it looks for it in the hash-table and removes it,
 * and sets its scratchpad, then never use the hash-table again.
 *
 * Therefore both side only use their scratchpad for the rest of the flow, that
 * is classified once for all 4-tuples functions (TCP and UDP based).
 */
class FlowIPNAT : public FlowStateElement<FlowIPNAT,NATEntryIN> , TCPHelper {
    public:

        FlowIPNAT() CLICK_COLD;
        ~FlowIPNAT() CLICK_COLD;

        const char *class_name() const override { return "FlowIPNAT"; }
        const char *port_count() const override { return "1/1"; }
        const char *processing() const override { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        int initialize(ErrorHandler *errh);

        //TCP only for now, just to reuse the macro but nothing prevents UDP
        FLOW_ELEMENT_DEFINE_SESSION_CONTEXT("12/0/ffffffff:HASH-3 16/0/ffffffff:HASH-3 22/0/ffff 20/0/ffff:ARRAY", FLOW_TCP);

        static const int timeout = NAT_FLOW_TIMEOUT;
        NATCommon* pick_port();
        bool new_flow(NATEntryIN*, Packet*);
        void release_flow(NATEntryIN*);

        void push_flow(int, NATEntryIN*, PacketBatch *);

    private:
        struct state {
            MPSCDynamicRing<NATCommon*> available_ports;
        };
        per_thread<state> _state;

        IPAddress _sip;
        bool _accept_nonsyn;
        bool _own_state;
        NATHashtable _map;
        friend class FlowIPNATReverse;
};


/**
 * See FlowIPNAT
 */
class FlowIPNATReverse : public FlowStateElement<FlowIPNATReverse,NATEntryOUT>, TCPHelper {
    public:
        FlowIPNATReverse() CLICK_COLD;
        ~FlowIPNATReverse() CLICK_COLD;

        const char *class_name() const override { return "FlowIPNATReverse"; }
        const char *port_count() const override { return "1/1"; }
        const char *processing() const override { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

        static const int timeout = NAT_FLOW_TIMEOUT;
        bool new_flow(NATEntryOUT*, Packet*);
        void release_flow(NATEntryOUT*);

        void push_flow(int, NATEntryOUT*, PacketBatch *);

    private:
        FlowIPNAT* _in;
};

/*
static inline bool update_state(WritablePacket* &q, T* flowdata)
{
    if (unlikely(q->tcp_header()->th_flags & TH_RST)) {
        return false;
    } else if (unlikely(q->tcp_header()->th_flags & TH_FIN)) {
        flowdata->fin_seen = true;
        if (flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK) {
            return false;
        } else {
            flowdata->ref->closing = true;
        }
    } else if (unlikely(flowdata->ref->closing && q->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen)) {
        return false;
    }
    return true;
}*/

template <typename T>
inline bool update_state(T* flowdata, const Packet* q)
{
#if HAVE_NAT_NEVER_REUSE
    if (unlikely(q->tcp_header()->th_flags & TH_RST)) {
        nat_info_chatter("RST");
        return false;
    } else if (unlikely((q->tcp_header()->th_flags & TH_FIN))) {
        if (flowdata->fin_seen) {
            // Dup FIN
            return true;
        }
        flowdata->fin_seen = true;
        lb_assert(flowdata->ref->closing <= 1);
        if (flowdata->ref->closing == 1 && q->tcp_header()->th_flags & TH_ACK) {
            nat_debug_chatter("FIN ACK");
            flowdata->ref->closing = 2;
            return false;
        } else {
            nat_debug_chatter("FIRST FIN");
            flowdata->ref->closing = 1;
            return true;
        }
        //unreachable
    /*} else if (unlikely(flowdata->ref->closing == 2 && q->tcp_header()->th_flags & TH_ACK && flowdata->fin_seen)) {
        nat_debug_chatter("CLOSING ACK");
        return false;
    }*/
        return true;
    }
    return true;
#else
    if ((q->tcp_header()->th_flags & TH_RST) || ((q->tcp_header()->th_flags & TH_FIN) && (q->tcp_header()->th_flags | TH_ACK))) {
#if DEBUG_NAT || DEBUG_CLASSIFIER_TIMEOUT > 1
        click_chatter("Reverse Rst %d, fin %d, ack %d",(q->tcp_header()->th_flags & TH_RST), (q->tcp_header()->th_flags & (TH_FIN)), (q->tcp_header()->th_flags & (TH_ACK)));
#endif
        return false;
    }
    return true;
#endif
    // unreachable
}

inline void release_ref(NATCommon* &ref, const bool &own_state)
{
    if (!ref)
        return;
    nat_debug_chatter("fcb->ref for port %d is %d, will be %d",ntohs(ref->port), ref->ref, ref->ref - 1);
    lb_assert((int32_t)ref->ref >= 1);
    if (!own_state)
        ref->closing = 1;
    if (ref->ref.dec_and_test()) {
        nat_debug_chatter("Recycling %d !", ntohs(ref->port));

        assert(ref->ref == 0);
        if (!ref->ring->insert(ref)) {
            click_chatter("Double free");
            abort();
        }
    }
    ref = 0;
}

CLICK_ENDDECLS
#endif
