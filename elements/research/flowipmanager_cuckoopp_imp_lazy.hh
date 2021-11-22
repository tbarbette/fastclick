#ifndef CLICK_FLOWIPMANAGER_CUKOOPPIMP_LAZY_HH
#define CLICK_FLOWIPMANAGER_CUKOOPPIMP_LAZY_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/flowelement.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>
#include <click/timerwheel.hh>
#include <click/virtualflowipmanagerimp.hh>




class FlowIPManager_CuckooPPIMP_lazy: public VirtualFlowIPManagerIMP<nolock,true,false,maintainerArgNone> {
    public:

        const char *class_name() const override { return "FlowIPManager_CuckooPPIMP_lazy"; }
        FlowIPManager_CuckooPPIMP_lazy(): VirtualFlowIPManagerIMP() { _timer = 0;}

	void run_timer(Timer *timer);

	int find(IPFlow5ID &f, int core) override;
	int insert(IPFlow5ID &f, int flowid, int core) override;
	int alloc(int i) override;
	int delete_flow(FlowControlBlock *fcb, int core = 0) override {
		abort();
	}
	// We are never full (although we may to deletion)
	inline bool insert_if_full() override { return LAZY_INSERT_FULL; }
	uint16_t _recent = 0;
	Timer * _timer;
	uint32_t _lazy_timeout;
	//int delete_flow(FlowControlBlock * fcb, int core) override;
	
#if IMP_COUNTERS
	struct lazytable {
	    uint32_t recycled_entries = 0;
	    uint32_t total_flows = 0;
	};
	lazytable* _lazytables = 0;
#endif

};

CLICK_ENDDECLS
#endif
