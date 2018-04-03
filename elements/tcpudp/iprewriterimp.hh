#ifndef CLICK_IPREWRITERIMP_HH
#define CLICK_IPREWRITERIMP_HH
#include "tcprewriterimp.hh"
#include "udprewriterimp.hh"
CLICK_DECLS
class UDPRewriterIMP;

/*
=c

IPRewriterIMP(INPUTSPEC1, ..., INPUTSPECn [, I<keywords>])

=s nat

rewrites TCP/UDP packets' addresses and ports in a thread-independent way. See IPRewriter.
*/

class IPRewriterIMP : public TCPRewriterIMP { public:

    IPRewriterIMP() CLICK_COLD;
    ~IPRewriterIMP() CLICK_COLD;

    const char *class_name() const override { return "IPRewriterIMP"; }
    void *cast(const char *) override;

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    IPRewriterEntry *get_entry(int ip_p, const IPFlowID &flowid, int input);
    HashContainer<IPRewriterEntry> *get_map(int mapid) {
	if (mapid == IPRewriterInput::mapid_default)
	    return &map();
	else if (mapid == IPRewriterInput::mapid_iprewriter_udp)
	    return &_ipstate->_udp_map;
	else
	    return 0;
    }
    IPRewriterEntry *add_flow(int ip_p, const IPFlowID &flowid,
			      const IPFlowID &rewritten_flowid, int input) override;
    void destroy_flow(IPRewriterFlow *flow) override;
    click_jiffies_t best_effort_expiry(const IPRewriterFlow *flow) override {
	if (((IPRewriterFlow*)flow)->ip_p() == IP_PROTO_TCP)
	    return TCPRewriterIMP::best_effort_expiry(flow);
	else
	    return ((IPRewriterFlow*)flow)->expiry() +
                  udp_flow_timeout(static_cast<const UDPFlow *>(flow), _ipstate.get()) - _ipstate->_udp_timeouts[1];
    }

    void push(int, Packet *) override;
#if HAVE_BATCH
    void push_batch(int port, PacketBatch *batch) override;
#endif

    void add_handlers() override CLICK_COLD;

  private:
    class IPState { public:
        IPState() : _udp_map(0) {
        }
        Map                                 _udp_map;
        SizedHashAllocator<sizeof(UDPFlow)> _udp_allocator;
        uint32_t                            _udp_timeouts[2];
        uint32_t                            _udp_streaming_timeout;
    };

    per_thread<IPState> _ipstate;

    int process(int port, Packet *p_in);

    int udp_flow_timeout(const UDPFlow *mf, IPState& state) const {
	if (mf->streaming())
	    return state._udp_streaming_timeout;
	else
	    return state._udp_timeouts[0];
    }

    static inline Map &reply_udp_map(IPRewriterInputIMP *rwinput) {
	IPRewriterIMP *x = (IPRewriterIMP *)(rwinput->reply_element());
	return x->_ipstate->_udp_map;
    }
    static String udp_mappings_handler(Element *e, void *user_data);

};


inline void
IPRewriterIMP::destroy_flow(IPRewriterFlow *flow)
{
    if (flow->ip_p() == IP_PROTO_TCP)
	TCPRewriterIMP::destroy_flow(flow);
    else {
	unmap_flow(flow, _ipstate->_udp_map, &reply_udp_map(flow->ownerimp()));
	flow->~IPRewriterFlow();
	_ipstate->_udp_allocator.deallocate(flow);
    }
}

CLICK_ENDDECLS
#endif
