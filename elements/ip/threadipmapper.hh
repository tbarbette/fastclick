#ifndef CLICK_THREADIPMAPPER_HH
#define CLICK_THREADIPMAPPER_HH
#include "elements/ip/iprewriterbase.hh"
CLICK_DECLS

/*
 * =c
 * ThreadIPMapper(PATTERN1, ..., PATTERNn)
 * =s nat
 * thread-based mapper for IPRewriter(n)
 * =d
 *
 * Works in tandem with IPRewriter to provide per-thread rewriting. This is
 * useful, for example, to write port-dependent NAT. Implements the
 * IPMapper interface.
 *
 * Responds to mapping requests from an IPRewriter by choosing a PATTERN
 * according to the current thread.
 *
 * =a IPRewriter, TCPRewriter, IPRewriterPatterns, RoundRobinIPMapper */

class ThreadIPMapper : public Element, public IPMapper { public:

    ThreadIPMapper() CLICK_COLD;
    ~ThreadIPMapper() CLICK_COLD;

    const char *class_name() const override	{ return "ThreadIPMapper"; }
    void *cast(const char *);

    int configure_phase() const		{ return IPRewriterBase::CONFIGURE_PHASE_MAPPER;}
    int configure(Vector<String> &conf, ErrorHandler *errh) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    void notify_rewriter(IPRewriterBase *user, IPRewriterInput *input,
			 ErrorHandler *errh);
    int rewrite_flowid(IPRewriterInput *input,
		       const IPFlowID &flowid, IPFlowID &rewritten_flowid,
		       Packet *p, int mapid);

 private:

    Vector<IPRewriterInput> _is;

};

CLICK_ENDDECLS
#endif
