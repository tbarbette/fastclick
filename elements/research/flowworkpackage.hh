// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FlowWorkPackage_HH
#define CLICK_FlowWorkPackage_HH
#include <click/batchelement.hh>
#include <click/sync.hh>
#include <random>

struct rte_hash;

CLICK_DECLS

/*
=c

FlowWorkPackage()

=s test

Do some heavy work (N random number computation) per-flow. The weight of a flow
is selected randomly per-flow. So some flows are heavy, some flows are light.

*/

class FlowWorkPackage : public BatchElement { public:

    FlowWorkPackage() CLICK_COLD;

    const char *class_name() const      { return "FlowWorkPackage"; }
    const char *port_count() const    { return "1-/="; }
    const char *processing() const    { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) override CLICK_COLD;
    int initialize(ErrorHandler*) override CLICK_COLD;

    void rmaction(Packet* p, int&);
    void push(int, Packet* p) override;
#if HAVE_BATCH
    void push_batch(int, PacketBatch* batch) override;
#endif

    void add_handlers() override CLICK_COLD;
private:

    static String read_handler(Element *e, void *thunk);

    unsigned _read_per_packet;
    unsigned _write_per_packet;
    unsigned _read_per_flow;
    unsigned _write_per_flow;
    unsigned _flow_state_size_full;
    unsigned _flow_state_size_user;

    unsigned _table_size;
    rte_hash* _table;
    void* _table_data;
    bool _sequential;
    int _order;
    int _w;
    bool _active;

    per_thread<std::mt19937> _gens;

    struct Stat {
    	Stat() : out_of_order(0) {

    	}

    	uint64_t out_of_order;
    };
    per_thread<Stat> _stats;

    //static String read_handler(Element *, void *) CLICK_COLD;
};

CLICK_ENDDECLS
#endif
