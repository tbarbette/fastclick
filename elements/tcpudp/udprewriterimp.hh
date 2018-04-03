#ifndef CLICK_UDPREWRITERIMP_HH
#define CLICK_UDPREWRITERIMP_HH
#include "elements/ip/iprewriterbaseimp.hh"
#include "udprewriter.hh"
#include <click/sync.hh>
CLICK_DECLS

/*
=c

UDPRewriterIMP(INPUTSPEC1, ..., INPUTSPECn [, I<keywords>])

=s nat

rewrites TCP/UDP packets' addresses and ports, independent multi processing

see UDPRewriter


=a UDPRewriter */

class UDPRewriterIMP : public IPRewriterBaseIMP { public:

    UDPRewriterIMP() CLICK_COLD;
    ~UDPRewriterIMP() CLICK_COLD;

    const char *class_name() const		{ return "UDPRewriterIMP"; }
    void *cast(const char *);

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    IPRewriterEntry *add_flow(int ip_p, const IPFlowID &flowid,
			      const IPFlowID &rewritten_flowid, int input) override;
    void destroy_flow(IPRewriterFlow *flow) override;
    click_jiffies_t best_effort_expiry(const IPRewriterFlow *flow) override {
	return ((IPRewriterFlow*)flow)->expiry() + udp_flow_timeout(static_cast<const UDPFlow *>((IPRewriterFlow*)flow)) -
               timeouts()[1];
    }

    void push(int, Packet *);
#if HAVE_BATCH
    void push_batch(int port, PacketBatch *batch);
#endif

    void add_handlers() CLICK_COLD;

  private:
    per_thread<SizedHashAllocator<sizeof(UDPFlow)>> _allocator;

    unsigned _annos;
    uint32_t _udp_streaming_timeout;

    int process(int port, Packet *p_in);

    int udp_flow_timeout(const UDPFlow *mf) const {
	if (mf->streaming())
	    return _udp_streaming_timeout;
	else
	    return timeouts()[0];
    }

    static String dump_mappings_handler(Element *, void *);

    friend class IPRewriter;

};


inline void
UDPRewriterIMP::destroy_flow(IPRewriterFlow *flow)
{
    unmap_flow(flow, map());
    flow->~IPRewriterFlow();
    _allocator->deallocate(flow);
}



CLICK_ENDDECLS
#endif
