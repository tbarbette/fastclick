#ifndef CLICK_L2LoadBalancer_HH
#define CLICK_L2LoadBalancer_HH
#include <click/config.h>
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/vector.hh>
#include <clicknet/ether.h>
#include <click/etheraddress.hh>
#include <click/loadbalancer.hh>



CLICK_DECLS


/**
=c

L2LoadBalancer([I<KEYWORDS>])

=s flow

Stateless L2 load-balancer

=d

Load-balancer that only rewrites the destination.

Keyword arguments are:

=over 8

=item DST

Ethernet Address. Can be repeated multiple times, once per destination.

=item LB_MODE

See FlowIPLoadBalancer. Remember this element is not stateful, so stateful load-balancing techniques will likely break PCC.

=back

=e
    L2LoadBalancer(DST 00:00:00:00:00:00, DST 00:00:00:00:00:01, DST 00:00:00:00:00:03)

=a

L2LoadBalancer, FlowIPNAT */

class L2LoadBalancer : public BatchElement, public LoadBalancer<EtherAddress> {
    public:
        L2LoadBalancer() CLICK_COLD;
        ~L2LoadBalancer() CLICK_COLD;

        const char *class_name() const { return "L2LoadBalancer"; }
        const char *port_count() const { return "1/1"; }
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
        int initialize(ErrorHandler *errh) override CLICK_COLD;

        void push_batch(int, PacketBatch *);
};

CLICK_ENDDECLS
#endif
