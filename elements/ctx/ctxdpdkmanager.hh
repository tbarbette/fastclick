#ifndef CLICK_CTXDPDKCLASSIFIER_HH
#define CLICK_CTXDPDKCLASSIFIER_HH
#include "ctxmanager.hh"
#include <click/vector.hh>
#include <rte_flow.h>
#include "../userlevel/fromdpdkdevice.hh"

class CTXDPDKManager : public CTXManager { public:
    CTXDPDKManager() CLICK_COLD;

	~CTXDPDKManager() CLICK_COLD;

    const char *class_name() const		{ return "CTXDPDKManager"; }
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

class CTXDPDKBuilderManager : public CTXDPDKManager { public:

    CTXDPDKBuilderManager() CLICK_COLD;

	~CTXDPDKBuilderManager() CLICK_COLD;

    const char *class_name() const		{ return "CTXDPDKBuilderManager"; }

    void push_batch(int port, PacketBatch* batch) override;

};

class CTXDPDKCacheManager : public CTXDPDKManager { public:

    CTXDPDKCacheManager() CLICK_COLD;

	~CTXDPDKCacheManager() CLICK_COLD;

    const char *class_name() const		{ return "CTXDPDKCacheManager"; }

    void push_batch(int port, PacketBatch* batch) override;

};
#endif
