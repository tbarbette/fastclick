#ifndef CLICK_FLOWIPMANAGERMP_TW_HH
#define CLICK_FLOWIPMANAGERMP_TW_HH
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
#include "flowipmanagermp.hh"
#include <rte_hash.h>

CLICK_DECLS

//DPDK hash tables, MP, timerwheel

class FlowIPManagerMP_TW : public  VirtualFlowIPManagerIMP<Spinlock,false,true,maintainerArgIMP>  {
public:

    const char *class_name() const override { return "FlowIPManagerMP_TW"; }
	FlowIPManagerMP_TW();

	int maintainer(int core) override;

	per_thread< TimerWheel<FlowControlBlock> > _timer_wheel;

	per_thread< FlowControlBlock* > _qbsr;
    inline static FlowControlBlock** get_next(FlowControlBlock *fcb) {
	    return (FlowControlBlock**) FCB_DATA(fcb,reserve_size() );
    };

    int parse(Args *args) override;
  protected:
	int find(IPFlow5ID &f, int core=0) override;
	int insert(IPFlow5ID &f, int flowid, int core=0) override;
	int alloc(int i) override;

	int delete_flow(FlowControlBlock * fcb, int core) override;
	int free_pos(int pos, int core) override;

	int _flags = 0;
    bool _lf;

};

CLICK_ENDDECLS
#endif
