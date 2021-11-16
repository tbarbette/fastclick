#ifndef CLICK_TCPSTATEIN_HH
#define CLICK_TCPSTATEIN_HH
#include <click/config.h>
#include <click/multithread.hh>
#include <click/hashtablemp.hh>
#include <click/allocator.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/ipflowid.hh>
CLICK_DECLS

#include <click/hashtablemp.hh>
#include <click/flow/flowelement.hh>

CLICK_DECLS

struct TCPStateCommon {
    TCPStateCommon() : closing(false) {
        use_count = 0;
    }
    atomic_uint32_t use_count; //Reference count
    bool closing; //Has one side started to close?
    uint32_t _pad[2];
};


/**
 * NAT Entries for the mapping side : a mapping to the original port
 * and a flag to know if the mapping has been seen on this side.
 */
struct TCPStateEntry {
    TCPStateCommon* common;
    bool fin_seen;
};

typedef HashTableMP<IPFlowID,TCPStateCommon*> TCPStateHashtable; //Table used to pass the mapping from the mapper to the reverse

#define TCP_STATE_FLOW_TIMEOUT 16 * 1000 //Flow timeout

/**
 * Efficient MiddleClick-based TCP State machine
 *
 * Working is similar to FLowIPNat
 */
class TCPStateIN : public FlowStateElement<TCPStateIN,TCPStateEntry> {

public:

    TCPStateIN() CLICK_COLD;
    ~TCPStateIN() CLICK_COLD;

    const char *class_name() const		{ return "TCPStateIN"; }
    const char *port_count() const		{ return "1/1-2"; }
    const char *processing() const		{ return PUSH; }

    //TCP only for now, just to reuse the macro but nothing prevents UDP
    FLOW_ELEMENT_DEFINE_SESSION_CONTEXT(DEFAULT_4TUPLE, FLOW_TCP);

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

    static void static_initialize();

    static const int timeout = TCP_STATE_FLOW_TIMEOUT;

    bool new_flow(TCPStateEntry*, Packet*);
    void release_flow(TCPStateEntry*);

    void push_flow(int, TCPStateEntry*, PacketBatch *);


    static String read_handler(Element* e, void* thunk);
    void add_handlers();
private:

    //Needs to be static to  prevent having one side releasing to another, hence having a pool only allocating and the other only cleaning
    static pool_allocator_mt<TCPStateCommon,false,16384> _pool;


    TCPStateHashtable _map;
    TCPStateIN* _return;
    bool _accept_nonsyn;
    int _verbose;
    atomic_uint32_t _established;
};

CLICK_ENDDECLS
#endif
