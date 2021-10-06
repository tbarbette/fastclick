#ifndef CLICK_FLOWROUTER_HH
#define CLICK_FLOWROUTER_HH
#include <click/string.hh>
#include <click/timer.hh>
#include "ctxmanager.hh"
#include <vector>
#include <click/flow/flowelement.hh>

CLICK_DECLS

class FlowRouter: public FlowSpaceElement<int> {

public:

	FlowRouter() CLICK_COLD;
	~FlowRouter() CLICK_COLD {};

    const char *class_name() const		{ return "FlowRouter"; }
    const char *port_count() const		{ return "1/-"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int, int* flowdata, PacketBatch* batch) override;

    FlowNode* get_table(int,Vector<FlowElement*>) override;

private :
    typedef struct {
        FlowNode* root;
        int output;
    } Rule;


    Vector<Rule> rules;
    FlowNode* _table;
    bool _verbose;

};


CLICK_ENDDECLS
#endif
