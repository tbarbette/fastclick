#ifndef CLICK_IPLoadBalancer_HH
#define CLICK_IPLoadBalancer_HH
#include <click/config.h>
#include <click/tcphelper.hh>
#include <click/multithread.hh>
#include <click/glue.hh>
#include <click/batchelement.hh>
#include <click/loadbalancer.hh>
#include <click/vector.hh>


CLICK_DECLS

class IPLoadBalancerReverse;

/**
=c

IPLoadBalancer([I<KEYWORDS>])

=s flow

TCP&UDP load-balancer, without SNAT

=d

Load-balancer than only rewrites the destination.

Keyword arguments are:

=over 8

=item DST

IP Address. Can be repeated multiple times, once per destination.

=item VIP
IP Address of this load-balancer.

=back


=a

FlowIPLoadBalancer, FlowIPNAT */

class IPLoadBalancer : public BatchElement, public TCPHelper, public LoadBalancer {

public:

    IPLoadBalancer() CLICK_COLD;
    ~IPLoadBalancer() CLICK_COLD;

    const char *class_name() const		{ return "IPLoadBalancer"; }
    const char *port_count() const		{ return "1/1"; }
    const char *processing() const		{ return PUSH; }


    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

#if HAVE_BATCH
    void push_batch(int, PacketBatch *) override;
#endif

    void push(int, Packet *) override;

    void add_handlers() override CLICK_COLD;

private:
    static int handler(int op, String& s, Element* e, const Handler* h, ErrorHandler* errh);
    static String read_handler(Element *handler, void *user_data);
    IPAddress _vip;
    bool _accept_nonsyn;
    static int write_handler(
      const String &, Element *, void *, ErrorHandler *
  ) CLICK_COLD;
    friend class LoadBalancer;
    friend class IPLoadBalancerReverse;

};


class IPLoadBalancerReverse : public BatchElement, public TCPHelper  {

public:

    IPLoadBalancerReverse() CLICK_COLD;
    ~IPLoadBalancerReverse() CLICK_COLD;

    const char *class_name() const      { return "IPLoadBalancerReverse"; }
    const char *port_count() const      { return "1/1"; }
    const char *processing() const      { return PUSH; }


    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *errh) override CLICK_COLD;

#if HAVE_BATCH
    void push_batch(int, PacketBatch *) override;
#endif

    void push(int, Packet *) override;
private:
    IPLoadBalancer* _lb;
};


CLICK_ENDDECLS
#endif
