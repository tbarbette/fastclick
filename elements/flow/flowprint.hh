#ifndef CLICK_FLOWPRINT_HH
#define CLICK_FLOWPRINT_HH
#include <click/batchelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <vector>
#include <click/flow/flow.hh>

CLICK_DECLS

/**
 * =c
 * FlowPrint
 * 
 * =s flow
 * 
 * Displays information about the FCB stack
 * 
 */
class FlowPrint: public BatchElement {
public:

    FlowPrint() {in_batch_mode = BATCH_MODE_NEEDED;} CLICK_COLD;

	~FlowPrint() {};

    const char *class_name() const		{ return "FlowPrint"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
    int initialize(ErrorHandler *errh) CLICK_COLD;

    void push_batch(int port, PacketBatch*);
private:
    bool _continue;
    bool _show_ptr;
};
CLICK_ENDDECLS
#endif
