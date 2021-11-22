#ifndef CLICK_FLOWIPMANAGERHH_HH
#define CLICK_FLOWIPMANAGERHH_HH
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
#include <click/hashtablemp.hh>

// This is a single thread version of HMP!

class FlowIPManagerH: public VirtualFlowIPManagerIMP<nolock,false,false,maintainerArgMP> {
    public:

        const char *class_name() const { return "FlowIPManagerH"; }
	FlowIPManagerH(): VirtualFlowIPManagerIMP() {}

    protected:
	int find(IPFlow5ID &f, int core) override;
	int insert(IPFlow5ID &f, int flowid, int core) override;
	int alloc(int i) override;
	int delete_flow(FlowControlBlock * fcb, int core) override;

};

CLICK_ENDDECLS
#endif
