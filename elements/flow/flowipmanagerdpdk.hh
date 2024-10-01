#ifndef CLICK_FLOWIPMANAGERDPDK_HH
#define CLICK_FLOWIPMANAGERDPDK_HH
#include <click/config.h>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/pair.hh>
#include <click/flow/common.hh>
#include <click/flow/virtualflowmanager.hh>
#include <click/batchbuilder.hh>
#include <click/timerwheel.hh>

CLICK_DECLS

class DPDKDevice;
struct rte_hash;

class FlowIPManager_DPDKState: public FlowManagerIMPStateNoFID { public:
    void *hash = 0;
} CLICK_ALIGNED(CLICK_CACHE_LINE_SIZE);

/**
 * =c
 * FlowIPManager_DPDK(CAPACITY [, RESERVE])
 *
 * =s flow
 *  FCB packet classifier - cuckoo per-thread
 *
 * =d
 *
 * Initialize the FCB stack for every packets passing by.
 * The classification is done using a per-core cuckoo hash table.
 *
 * This element does not find automatically the FCB layout for FlowElement,
 * neither set the offsets for placement in the FCB automatically. Look at
 * the middleclick branch for alternatives.
 *
 * =a FlowIPManger
 *
 */
class FlowIPManager_DPDK: public VirtualFlowManagerIMP<FlowIPManager_DPDK, FlowIPManager_DPDKState>
{
    public:
        FlowIPManager_DPDK() CLICK_COLD;
        ~FlowIPManager_DPDK() CLICK_COLD;

        const char *class_name() const override { return "FlowIPManager_DPDK"; }
        const char *port_count() const override { return "1/1"; }

        const char *processing() const override { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        void cleanup(CleanupStage stage) override CLICK_COLD;

    protected:

    //Implemented for VirtualFlowManagerIMP. It is using CRTP so no override.
    inline int alloc(FlowIPManager_DPDKState& table, int core, ErrorHandler* errh);
	inline int find(IPFlow5ID &f);
	inline int insert(IPFlow5ID &f, int);
    inline int remove(IPFlow5ID &f);
    inline int count();

    friend class VirtualFlowManagerIMP;
};

CLICK_ENDDECLS

#endif

