#ifndef CLICK_FLOWDISPATCHER_HH
#define CLICK_FLOWDISPATCHER_HH
#include <click/flowelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include "flowclassifier.hh"
#include <vector>

CLICK_DECLS

class FlowDispatcher: public FlowBufferElement<int> {

public:

	FlowDispatcher() CLICK_COLD;
	~FlowDispatcher() CLICK_COLD {};

    const char *class_name() const		{ return "FlowDispatcher"; }
    const char *port_count() const		{ return "1/-"; }
    const char *processing() const		{ return PUSH; }
    const char *flow_code() const		{ return "x/x"; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int, int* flowdata, PacketBatch* batch);

    FlowNode* get_table();

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
