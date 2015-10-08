#ifndef CLICK_FLOWPRINT_HH
#define CLICK_FLOWPRINT_HH
#include <click/batchelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <click/flow.hh>
#include <vector>

CLICK_DECLS

class FlowPrint: public BatchElement {
public:

    FlowPrint() {} CLICK_COLD;

	~FlowPrint() {};

    const char *class_name() const		{ return "FlowPrint"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }
    const char *flow_code() const		{ return "x/x"; }
    bool need_batch() const		{ return true; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int port, PacketBatch*);

};
CLICK_ENDDECLS
#endif
