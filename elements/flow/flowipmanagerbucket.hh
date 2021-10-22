#ifndef CLICK_FLOWIPMANAGERBUCKET_HH
#define CLICK_FLOWIPMANAGERBUCKET_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/batchelement.hh>
#include <click/pair.hh>
#include <click/batchbuilder.hh>
#include <click/flow/common.hh>
#include <click/flow/flowelement.hh>
#include <nicscheduler/ethernetdevice.hh>
#include <nicscheduler/nicscheduler.hh>
#include <utility>

CLICK_DECLS
class DPDKDevice;
struct rte_hash;

/**
 * FCB packet classifier - cuckoo per-thread
 *
 * Initialize the FCB stack for every packets passing by.
 * The classification is done using one cuckoo hashtable per threads. Hence,
 * when some flows are migratedthe do_migrate function must be called.
 *
 * This element does not find automatically the FCB layout for FlowElement,
 * neither set the offsets for placement in the FCB automatically. Look at
 * the middleclick branch for alternatives.
 */
class FlowIPManagerBucket: public VirtualFlowManager, public Router::ChildrenFuture, public MigrationListener {
public:

    FlowIPManagerBucket() CLICK_COLD;
	~FlowIPManagerBucket() CLICK_COLD;

    const char *class_name() const		{ return "FlowIPManagerBucket"; }
    const char *port_count() const		{ return "1/1"; }

    const char *processing() const		{ return PUSH; }
    int configure_phase() const     { return CONFIGURE_PHASE_PRIVILEGED + 1; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int solve_initialize(ErrorHandler *errh) override CLICK_COLD;
    void cleanup(CleanupStage stage) CLICK_COLD;

    //First : group id, second : destination cpu
    void pre_migrate(EthernetDevice* dev, int from, std::vector<std::pair<int,int>> gids) override;
    void post_migrate(EthernetDevice* dev, int from) override;
    void init_assignment(unsigned* table, int sz) override;

    void push_batch(int, PacketBatch* batch);



    void add_handlers();

    static String read_handler(Element* e, void* thunk);
private:

    struct CoreInfo {
        CoreInfo() : count(0), watch(0), lock(), pending(false) {
	};
	uint64_t count;
	uint64_t watch;
	Spinlock lock;
	bool pending;
	Vector<std::pair<int,int> > moves; //First : group id, second : destination cpu
    } CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

    struct gtable {
	gtable() : queue(0) {

	}
	volatile int owner;
	Packet* queue;
	rte_hash* hash;
        FlowControlBlock *fcbs;
    } CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);


    int _groups;
    int _table_size;
    int _flow_state_size_full;
    int _verbose;
    int _def_thread;
    bool _mark;
    bool _do_migration;

    gtable* _tables;
    per_thread<CoreInfo> _cores;

    void do_migrate(CoreInfo &core);
    inline void flush_queue(int groupid, BatchBuilder &b);
    inline void process(int groupid, Packet* p, BatchBuilder& b);

};

CLICK_ENDDECLS
#endif
