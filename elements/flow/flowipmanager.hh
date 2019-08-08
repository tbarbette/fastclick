#ifndef CLICK_FlowIPManager_HH
#define CLICK_FlowIPManager_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/batchelement.hh>
#include <click/pair.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>

CLICK_DECLS
class DPDKDevice;
struct rte_hash;

/**
 * FlowIPManager(CAPACITY [, RESERVE])
 *
 * =s flow
 *  FCB packet classifier - cuckoo shared-by-all-threads
 *
 * =d
 *
 * Initialize the FCB stack for every packets passing by.
 * The classification is done using a unique cuckoo hash table.
 *
 * This element does not find automatically the FCB layout for FlowElement,
 * neither set the offsets for placement in the FCB automatically. Look at
 * the middleclick branch for alternatives.
 *
 * =a FlowIPManger
 *
 */
class FlowIPManager: public BatchElement {
public:


    FlowIPManager() CLICK_COLD;

	~FlowIPManager() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPManager"; }
    const char *port_count() const		{ return "1/1"; }

    const char *processing() const		{ return PUSH; }
    int configure_phase() const     { return CONFIGURE_PHASE_PRIVILEGED + 1; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;
    void cleanup(CleanupStage stage) CLICK_COLD;

    void push_batch(int, PacketBatch* batch) override;

    void add_handlers() override CLICK_COLD;

protected:


	volatile int owner;
	Packet* queue;
	rte_hash* hash;
	FlowControlBlock *fcbs;


    int _reserve;
    int _table_size;
    int _flow_state_size_full;
    int _verbose;
    int _flags;

    String read_handler(Element* e, void* thunk);
    inline void process(Packet* p, BatchBuilder& b);

};



CLICK_ENDDECLS
#endif
