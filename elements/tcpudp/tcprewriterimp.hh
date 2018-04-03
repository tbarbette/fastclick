#ifndef CLICK_TCPREWRITERIMP_HH
#define CLICK_TCPREWRITERIMP_HH
#include "elements/ip/iprewriterbaseimp.hh"
#include "tcprewriter.hh"
#include <clicknet/tcp.h>
CLICK_DECLS

/*
=c

TCPRewriterIMP(INPUTSPEC1, ..., INPUTSPECn [, KEYWORDS])

=s nat

rewrites TCP packets' addresses, ports, and sequence numbers in a thread-independent way. See TCPRewriter for more informations.

*/

class TCPRewriterIMP : public IPRewriterBaseIMP { public:

    TCPRewriterIMP() CLICK_COLD;
    ~TCPRewriterIMP() CLICK_COLD;

    const char *class_name() const override	{ return "TCPRewriterIMP"; }
    void *cast(const char *) override;

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    IPRewriterEntry *add_flow(int ip_p, const IPFlowID &flowid,
			      const IPFlowID &rewritten_flowid, int input) override;
    void destroy_flow(IPRewriterFlow *flow) override;
    click_jiffies_t best_effort_expiry(const IPRewriterFlow *flow) override {
	return flow->expiry() + tcp_flow_timeout(static_cast<const TCPFlow *>((IPRewriterFlow*)flow)) -
               timeouts()[1];
    }

    void push(int, Packet *) override;
#if HAVE_BATCH
     void push_batch(int port, PacketBatch *batch) override;
#endif

    void add_handlers() override CLICK_COLD;

 protected:
    per_thread<SizedHashAllocator<sizeof(TCPFlow)>> _allocator;

    unsigned _annos;
    uint32_t _tcp_data_timeout;
    uint32_t _tcp_done_timeout;

    /**
     * The actual processing of this element is abstracted from the push operation.
     * This allows both push and push_batch to exploit the same logic.
     */
    int process(int port, Packet *p_in);

    int tcp_flow_timeout(const TCPFlow *mf) const {
	if (mf->both_done())
	    return _tcp_done_timeout;
	else if (mf->both_data())
	    return _tcp_data_timeout;
	else
	    return timeouts()[0];
    }

    static String tcp_mappings_handler(Element *, void *);
    static int tcp_lookup_handler(int, String &str, Element *e, const Handler *h, ErrorHandler *errh);

};

inline void
TCPRewriterIMP::destroy_flow(IPRewriterFlow *flow)
{
    unmap_flow(flow, map());
    static_cast<TCPFlow *>(flow)->~TCPFlow();
    _allocator->deallocate(flow);
}

CLICK_ENDDECLS
#endif
