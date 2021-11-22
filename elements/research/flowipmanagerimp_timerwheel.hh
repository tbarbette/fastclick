#ifndef CLICK_FLOWIPMANAGERIMP_TW_HH
#define CLICK_FLOWIPMANAGERIMP_TW_HH
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

CLICK_DECLS

// DPDK hash tables, per core duplication, with timer wheel


class FlowIPManagerIMP_TW: public VirtualFlowIPManagerIMP<nolock,true,false,maintainerArgIMP> {
  public:

    const char *class_name() const { return "FlowIPManagerIMP_TW"; }

	FlowIPManagerIMP_TW(int flags = 0)
		: VirtualFlowIPManagerIMP(), _flags(flags) {}
	int maintainer(int core) override;

	per_thread< TimerWheel<FlowControlBlock> > _timer_wheel;

    inline static FlowControlBlock** get_next(FlowControlBlock *fcb) {
	    return (FlowControlBlock**) FCB_DATA(fcb,reserve_size() );
    };

    int parse(Args *args) override;
  protected:
	int find(IPFlow5ID &f, int core) override;
	int insert(IPFlow5ID &f, int flowid, int core) override;
	int alloc(int i) override;

	int delete_flow(FlowControlBlock * fcb, int core) override;
	int free_pos(int pos, int core) override;

	int _flags = 0;

};

CLICK_ENDDECLS
#endif
