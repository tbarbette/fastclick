#ifndef CLICK_FLOWROUTER_HH
#define CLICK_FLOWROUTER_HH
#include <click/string.hh>
#include <click/timer.hh>
#include <vector>
#include <click/flow/flowelement.hh>

CLICK_DECLS

class FlowRoundRobinSwitch: public FlowSpaceElement<int> {

public:

	FlowRoundRobinSwitch() CLICK_COLD;
	~FlowRoundRobinSwitch() CLICK_COLD {};

    const char *class_name() const		{ return "FlowRoundRobinSwitch"; }
    const char *port_count() const		{ return "1/-"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_flow(int, int* flowdata, PacketBatch* batch) override;


private :
    per_thread<int> _rr;

};


CLICK_ENDDECLS
#endif
