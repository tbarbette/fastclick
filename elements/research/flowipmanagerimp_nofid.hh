#ifndef CLICK_FLOWIPMANAGERIMPNOFID_HH
#define CLICK_FLOWIPMANAGERIMPNOFID_HH
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

// DPK hash tables, per core duplication

class FlowIPManagerIMP_NoFID: public VirtualFlowIPManagerIMP<nolock, true, false, maintainerArgIMPNOFID> {
    public:

        const char *class_name() const { return "FlowIPManagerIMP_NoFID"; }
	FlowIPManagerIMP_NoFID(): VirtualFlowIPManagerIMP() {}

    protected:
	int find(IPFlow5ID &f, int core) override;
	int insert(IPFlow5ID &f, int flowid, int core) override;
	int alloc(int i) override;
	int delete_flow(FlowControlBlock * fcb, int core) override;
	int free_pos(int pos, int core) override;
	int get_count(int core) override;

	uint64_t max_key_id(int core) override;


	int _flags = 0;

};

CLICK_ENDDECLS
#endif
