#ifndef CLICK_RRIPMAPPER_HH
#define CLICK_RRIPMAPPER_HH
#include "elements/ip/iprewriterbaseimp.hh"
CLICK_DECLS

/*
 * =c
 * RoundRobinIPMapper(PATTERN1, ..., PATTERNn)
 * =s nat
 * round-robin mapper for IPRewriter(n)
 * =d
 *
 * Works in tandem with IPRewriter to provide round-robin rewriting. This is
 * useful, for example, in load-balancing applications. Implements the
 * IPMapper interface.
 *
 * Responds to mapping requests from an IPRewriter by trying the PATTERNs in
 * round-robin order and returning the first successfully created mapping.
 *
 * =a IPRewriter, TCPRewriter, IPRewriterPatterns */

class RoundRobinIPMapperBase : public Element { public:
    int configure_phase() const	override { return IPRewriterBase::CONFIGURE_PHASE_MAPPER;}
    int configure(Vector<String> &conf, ErrorHandler *errh) override CLICK_COLD;

    void cleanup(CleanupStage) override CLICK_COLD;
 protected:

    Vector<IPRewriterInput> _is;
    int _last_pattern;
};


class RoundRobinIPMapper : public RoundRobinIPMapperBase, public IPMapper { public:

    RoundRobinIPMapper() CLICK_COLD;
    ~RoundRobinIPMapper() CLICK_COLD;

    const char *class_name() const override { return "RoundRobinIPMapper"; }
    void *cast(const char *);

    void notify_rewriter(IPRewriterBaseAncestor *user, IPRewriterInput *input,
			 ErrorHandler *errh) override;
    int rewrite_flowid(IPRewriterInputAncestor *input,
		       const IPFlowID &flowid, IPFlowID &rewritten_flowid,
		       Packet *p, int mapid) override;

};

class RoundRobinIPMapperIMP : public RoundRobinIPMapperBase, public IPMapperIMP { public:

	RoundRobinIPMapperIMP() CLICK_COLD;
    ~RoundRobinIPMapperIMP() CLICK_COLD;

    const char *class_name() const override { return "RoundRobinIPMapperIMP"; }
    void *cast(const char *) override;

    void notify_rewriter(IPRewriterBaseAncestor *user, IPRewriterInput *input,
			 ErrorHandler *errh) override;
    int rewrite_flowid(IPRewriterInputAncestor *input,
		       const IPFlowID &flowid, IPFlowID &rewritten_flowid,
		       Packet *p, int mapid) override;

};

CLICK_ENDDECLS
#endif
