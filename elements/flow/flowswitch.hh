#ifndef CLICK_FLOWSWITCH_HH
#define CLICK_FLOWSWITCH_HH
#include <click/config.h>
#include <click/tcphelper.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/loadbalancer.hh>
#include <click/vector.hh>

#include <click/flow/flowelement.hh>
#include "flowipnat.hh"

#define LB_FLOW_TIMEOUT 60 * 1000

CLICK_DECLS

struct FlowSwitchEntry {
    int chosen_server;
    FlowSwitchEntry(int addr) : chosen_server(addr) {
    }

};


/**
=c

FlowSwitch([I<KEYWORDS>])

=s flow

Port load-balancer

=d

Load-balancer among Click ports. The same modes than FlowIPLoadBalancer are available, but instead
of rewriting IP adresses this elements forward to specific ports.

Keyword arguments are:

=over 8


=back

=e
    fl :: FlowSwitch(MODE RR);
    fl[0] -> ...;
    fl[1] -> ...;

=a

FlowIPLoadBalancer, FlowIPNAT */

class FlowSwitch : public FlowStateElement<FlowSwitch,FlowSwitchEntry>,
                           public TCPHelper, public LoadBalancer {
    public:
        FlowSwitch() CLICK_COLD;
        ~FlowSwitch() CLICK_COLD;

        const char *class_name() const { return "FlowSwitch"; }
        const char *port_count() const { return "1/1-"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        static const int timeout = LB_FLOW_TIMEOUT;
        bool new_flow(FlowSwitchEntry*, Packet*);
        void release_flow(FlowSwitchEntry*) {};

        void push_batch(int, FlowSwitchEntry*, PacketBatch *);

	void add_handlers() override;

private:
	static int handler(int op, String& s, Element* e, const Handler* h, ErrorHandler* errh);
        static String read_handler(Element *handler, void *user_data);
        static int write_handler(
            const String &, Element *, void *, ErrorHandler *
            ) CLICK_COLD;

	friend class LoadBalancer;
};

CLICK_ENDDECLS
#endif
