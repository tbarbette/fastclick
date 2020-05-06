#ifndef CLICK_FLOWIPLOADBALANCER_HH
#define CLICK_FLOWIPLOADBALANCER_HH
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

struct IPLBEntry {
    int chosen_server;
    IPLBEntry(int addr) : chosen_server(addr) {
    }

    inline hashcode_t hashcode() const {
        return CLICK_NAME(hashcode)(chosen_server);
    }

    inline bool operator==(IPLBEntry other) const {
        return (other.chosen_server == chosen_server);
    }
};

class FlowIPLoadBalancerReverse;

/**
=c

FlowIPLoadBalancer([I<KEYWORDS>])

=s flow

TCP & UDP load-balancer, without SNAT

=d

Load-balancer that only rewrites the destination.

Keyword arguments are:

=over 8

=item DST

IP Address. Can be repeated multiple times, once per destination.

=item VIP
IP Address of this load-balancer.

=back

=e
    FlowIPLoadBalancer(VIP 10.220.0.1, DST 10.221.0.1, DST 10.221.0.2, DST 10.221.0.3)

=a

FlowIPLoadBalancer, FlowIPNAT */

class FlowIPLoadBalancer : public FlowStateElement<FlowIPLoadBalancer,IPLBEntry>,
                           public TCPHelper, public LoadBalancer {
    public:
        FlowIPLoadBalancer() CLICK_COLD;
        ~FlowIPLoadBalancer() CLICK_COLD;

        const char *class_name() const { return "FlowIPLoadBalancer"; }
        const char *port_count() const { return "1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        static const int timeout = LB_FLOW_TIMEOUT;
        bool new_flow(IPLBEntry*, Packet*);
        void release_flow(IPLBEntry*) {};

        void push_batch(int, IPLBEntry*, PacketBatch *);

    private:
        IPAddress _vip;
        bool _accept_nonsyn;

        friend class FlowIPLoadBalancerReverse;
};

struct SNull {

};

/**
=c

FlowIPLoadBalancerReverse(LB)

=s flow

Reverse side for FlowIPLoadBalancerReverse.

=d

Keyword arguments are:

=over 8

=item DST

IP Address. Can be repeated multiple times, once per destination.

=item VIP
IP Address of this load-balancer.

=back

=e
    FlowIPLoadBalancer(VIP 10.220.0.1, DST 10.221.0.1, DST 10.221.0.2, DST 10.221.0.3)

=a

FlowIPLoadBalancer, FlowIPNAT */

class FlowIPLoadBalancerReverse : public BatchElement {
    public:
        FlowIPLoadBalancerReverse() CLICK_COLD;
        ~FlowIPLoadBalancerReverse() CLICK_COLD;

        const char *class_name() const { return "FlowIPLoadBalancerReverse"; }
        const char *port_count() const { return "1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        void push_batch(int, PacketBatch *) override;

    private:
        FlowIPLoadBalancer* _lb;
};

CLICK_ENDDECLS
#endif
