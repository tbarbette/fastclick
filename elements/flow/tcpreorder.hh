#ifndef CLICK_TCPREORDER_HH
#define CLICK_TCPREORDER_HH
#include <click/flow.hh>
#include <click/flowelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <vector>

CLICK_DECLS

class TCPReorderFlowData {
	public:
		PacketBatch* waiting_for_new;
		uint32_t awaiting_seq;

};

class TCPReorder: public FlowBufferElement<TCPReorderFlowData> {
	int _timeout;

public:

	TCPReorder() : _timeout(0) {} CLICK_COLD;

	~TCPReorder() {};

    const char *class_name() const		{ return "TCPReorder"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }
    const char *flow_code() const		{ return "x/x"; }


    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int port, TCPReorderFlowData*, PacketBatch*);


 private:
    PacketBatch* reorder_list(PacketBatch* p);

};
CLICK_ENDDECLS
#endif
