#ifndef CLICK_FLOWDPDKCLASSIFIER_HH
#define CLICK_FLOWDPDKCLASSIFIER_HH
#include "flowmanager.hh"
#include <click/vector.hh>
#include <rte_flow.h>
#include "../userlevel/fromdpdkdevice.hh"

class FlowDPDKManager : public FlowManager { public:
    FlowDPDKManager() CLICK_COLD;

	~FlowDPDKManager() CLICK_COLD;

    const char *class_name() const		{ return "FlowDPDKManager"; }
    void* cast(const char *n) override;

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int port, PacketBatch* batch) override;
private:

    void add_rule(Vector<rte_flow_item> pattern, FlowNodePtr ptr);
    int traverse_rules(FlowNode* node, Vector<rte_flow_item> pattern, rte_flow_item_type last_layer, int offset);

protected:
    Vector<FlowNodePtr> _matches;
    FromDPDKDevice* _dev;
};

class FlowDPDKBuilderManager : public FlowDPDKManager { public:

    FlowDPDKBuilderManager() CLICK_COLD;

	~FlowDPDKBuilderManager() CLICK_COLD;

    const char *class_name() const		{ return "FlowDPDKBuilderManager"; }

    void push_batch(int port, PacketBatch* batch) override;

};

class FlowDPDKCacheManager : public FlowDPDKManager { public:

    FlowDPDKCacheManager() CLICK_COLD;

	~FlowDPDKCacheManager() CLICK_COLD;

    const char *class_name() const		{ return "FlowDPDKCacheManager"; }

    void push_batch(int port, PacketBatch* batch) override;

};
#endif
