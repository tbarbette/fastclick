#ifndef CLICK_FLOWIPMANAGERIMP_LAZY_HH
#define CLICK_FLOWIPMANAGERIMP_LAZY_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/flowelement.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>
#include <click/virtualflowipmanagerimp.hh>
#include <rte_hash.h>


// DPDK hash tables, per core duplication
// Lazy deletion by using a timestamp in the dpdk ht implementation.

class FlowIPManagerIMP_lazy: public VirtualFlowIPManagerIMP<nolock,true,false,maintainerArgNone> {
    public:

        const char *class_name() const { return "FlowIPManagerIMP_lazy"; }
        FlowIPManagerIMP_lazy(): VirtualFlowIPManagerIMP() { _timer = 0;}
	int parse(Args * args) override;
	int maintainer(int core) override;

	// Allow to define additional handlers
	static String read_handler(Element *e, void *thunk);
	virtual void add_handlers() override;
	String get_lazy_counter(int cnt);
	void run_timer(Timer *timer);

	// We don't need lastseen for the lazy methods...
	virtual inline void update_lastseen(FlowControlBlock *fcb,
                                      Timestamp &ts) override {};

    protected:
	int find(IPFlow5ID &f, int core=0) override;
	int insert(IPFlow5ID &f, int flowid, int core=0) override;
	int alloc(int i) override;
	int delete_flow(FlowControlBlock * fcb, int core) override;
	int free_pos(int pos, int core) override;

	int _flags = 0;
	bool _always_recycle;

	// We are never full (although we may to deletion)
	inline bool insert_if_full() override { return LAZY_INSERT_FULL; }


#if IMP_COUNTERS
	struct lazytable {
	    uint32_t recycled_entries = 0;
	    uint32_t total_flows = 0;
	};
	lazytable* _lazytables = 0;
#endif

	uint16_t _recent = 0;
	Timer * _timer;
};

CLICK_ENDDECLS
#endif
