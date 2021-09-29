#ifndef CLICK_FlowL2LoadBalancer_HH
#define CLICK_FlowL2LoadBalancer_HH
#include <click/config.h>
#include <click/tcphelper.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/loadbalancer.hh>
#include <click/vector.hh>
#include <click/etheraddress.hh>
#include <click/flow/flowelement.hh>
#include "flowipnat.hh"

#define LB_FLOW_TIMEOUT 60 * 1000

CLICK_DECLS

struct L2LBEntry {
    int chosen_server;
    L2LBEntry(int addr) : chosen_server(addr) {
    }

    inline hashcode_t hashcode() const {
        return CLICK_NAME(hashcode)(chosen_server);
    }

    inline bool operator==(L2LBEntry other) const {
        return (other.chosen_server == chosen_server);
    }
};

class FlowL2LoadBalancerReverse;

/**
=c

FlowL2LoadBalancer([I<KEYWORDS>])

=s flow

TCP & UDP load-balancer, without SNAT

=d

Load-balancer that only rewrites the destination.

Keyword arguments are:

=over 8

=item DST

Ethernet Address. Can be repeated multiple times, once per destination.

=item LB_MODE, LST_MODE, AWRR_TIME, ...
Load balancing mode and various parameters. See FlowIPLoadBalancer.

=back

=e
    FlowL2LoadBalancer(DST 00:00:00:00:00:00, DST 00:00:00:00:00:01, DST 00:00:00:00:00:03)

=a

L2LoadBalancer, FlowIPLoadBalancer, FlowIPNAT */

class FlowL2LoadBalancer : public FlowStateElement<FlowL2LoadBalancer,L2LBEntry>,
                           public TCPHelper, public LoadBalancer<EtherAddress> {
    public:
        FlowL2LoadBalancer() CLICK_COLD;
        ~FlowL2LoadBalancer() CLICK_COLD;

        const char *class_name() const { return "FlowL2LoadBalancer"; }
        const char *port_count() const { return "1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        static const int timeout = LB_FLOW_TIMEOUT;
        bool new_flow(L2LBEntry*, Packet*);
        void release_flow(L2LBEntry*) {};

        void push_flow(int, L2LBEntry*, PacketBatch *);

};

CLICK_ENDDECLS
#endif
