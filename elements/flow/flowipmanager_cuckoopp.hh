#ifndef CLICK_FLOWIPMANAGER_CUKOOPPIMP_FC_HH
#define CLICK_FLOWIPMANAGER_CUKOOPPIMP_FC_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/virtualflowmanager.hh>
#include <click/flow/common.hh>
#include <click/batchbuilder.hh>

CLICK_DECLS

class FlowIPManager_CuckooPPState: public FlowManagerIMPState { public:
    void *hash = 0;
} CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

class FlowIPManager_CuckooPP: public VirtualFlowManagerIMP<FlowIPManager_CuckooPP, FlowIPManager_CuckooPPState>
{
public:

    const char *class_name() const override { return "FlowIPManager_CuckooPP"; }
    const char *port_count() const override { return "1/1"; }

    const char *processing() const override { return PUSH; }

    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;
    void cleanup(CleanupStage stage) override CLICK_COLD;

 
protected:

    //Implemented for VirtualFlowManagerIMP. It is using CRTP so no override.
    inline int alloc(FlowIPManager_CuckooPPState& table, int core, ErrorHandler* errh);
	inline int find(IPFlow5ID &f);
	inline int insert(IPFlow5ID &f, int flowid);
    inline int remove(IPFlow5ID &f);
    inline int count();
    
    friend class VirtualFlowManagerIMP;
};

CLICK_ENDDECLS

#endif