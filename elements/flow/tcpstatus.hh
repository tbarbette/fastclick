#ifndef CLICK_TCPStatus_HH
#define CLICK_TCPStatus_HH
#include <click/flow.hh>
#include <click/flowelement.hh>
#include <click/string.hh>
#include <click/timer.hh>
#include <vector>

CLICK_DECLS

enum TcpStatus {TCP_FIRSTSEEN = 0,TCP_DROP};
class TCPStatusFlowData {
	public:
		enum TcpStatus status;

};

class TCPStatus: public FlowBufferElement<TCPStatusFlowData> {
	int _timeout;

public:

	TCPStatus() : _timeout(0) {} CLICK_COLD;

	~TCPStatus() {};

    const char *class_name() const		{ return "TCPStatus"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }
    const char *flow_code() const		{ return "x/x"; }


    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_batch(int port, TCPStatusFlowData*, PacketBatch*);


 private:

};
CLICK_ENDDECLS
#endif
