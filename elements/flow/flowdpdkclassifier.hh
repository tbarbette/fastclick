#ifndef CLICK_FLOWDPDKCLASSIFIER_HH
#define CLICK_FLOWDPDKCLASSIFIER_HH
#include "flowclassifier.hh"
#include <click/vector.hh>
#include <rte_flow.h>
#include "../userlevel/fromdpdkdevice.hh"

class FlowDPDKClassifier : public FlowClassifier { public:
    FlowDPDKClassifier() CLICK_COLD;

	~FlowDPDKClassifier() CLICK_COLD;

    const char *class_name() const		{ return "FlowDPDKClassifier"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int port, PacketBatch* batch) override;
private:

    void add_rule(Vector<rte_flow_item> pattern, FlowNodePtr ptr);
    int traverse_rules(FlowNode* node, Vector<rte_flow_item> &pattern, rte_flow_item_type last_layer, int offset);

    Vector<FlowNodePtr> _matches;
    FromDPDKDevice* _dev;
};
#endif
