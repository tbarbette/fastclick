#ifndef CLICK_FLOWIPMANAGERMP_HH
#define CLICK_FLOWIPMANAGERMP_HH
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

class FlowIPManagerMP: public VirtualFlowIPManagerIMP<Spinlock,false,true,maintainerArgMP> {
  public:

    const char *class_name() const { return "FlowIPManagerMP"; }
	// By default, with no_free_on_delete and deferred free
	FlowIPManagerMP(): VirtualFlowIPManagerIMP() {}

    protected:
	int find(IPFlow5ID &f, int core) override;
	int insert(IPFlow5ID &f, int flowid, int core) override;
	int alloc(int i) override;
	int delete_flow(FlowControlBlock * fcb, int core) override;
	int free_pos(int pos, int core) override;
	int parse(Args * args) override;


	int _flags;
	bool _lf;


};


CLICK_ENDDECLS
#endif
