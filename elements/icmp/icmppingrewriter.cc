/*
 * icmppingrewriter.{cc,hh} -- rewrites ICMP echoes and replies
 * Eddie Kohler
 *
 * Per-core, thread safe data structures by Georgios Katsikas and batching by
 *  Tom Barbette
 *
 * Copyright (c) 2000-2001 Mazu Networks, Inc.
 * Copyright (c) 2009-2010 Meraki, Inc.
 * Copyright (c) 2016 KTH Royal Institute of Technology
 * Copyright (c) 2017 University of Liège
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "icmppingrewriter.hh"
#include <clicknet/ip.h>
#include <clicknet/icmp.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
CLICK_DECLS

// ICMPPingMapping

void
ICMPPingRewriter::ICMPPingFlow::apply(WritablePacket *p, bool direction, unsigned annos)
{
    assert(p->has_network_header());
    click_ip *iph = p->ip_header();

    // IP header
    const IPFlowID &revflow = _e[!direction].flowid();
    iph->ip_src = revflow.daddr();
    iph->ip_dst = revflow.saddr();
    if (annos & 1)
	p->set_dst_ip_anno(revflow.saddr());
    if (direction && (annos & 2))
	p->set_anno_u8(annos >> 2, _reply_anno);
    update_csum(&iph->ip_sum, direction, _ip_csum_delta);

    // end if not first fragment
    if (!IP_FIRSTFRAG(iph))
	return;

    // ICMP header
    click_icmp_echo *icmph = reinterpret_cast<click_icmp_echo *>(p->transport_header());
    icmph->icmp_identifier = (direction ? revflow.sport() : revflow.dport());
    update_csum(&icmph->icmp_cksum, direction, _udp_csum_delta);
    click_update_zero_in_cksum(&icmph->icmp_cksum, p->transport_header(), p->transport_length());
}

void
ICMPPingRewriter::ICMPPingFlow::unparse(StringAccum &sa, bool direction,
					click_jiffies_t now) const
{
    const IPFlowID &flow = _e[direction].flowid();
    IPFlowID rewritten_flow = _e[direction].rewritten_flowid();
    sa << '(' << flow.saddr() << ", " << flow.daddr() << ", "
       << ntohs(flow.sport()) << ") => ("
       << rewritten_flow.saddr() << ", " << rewritten_flow.daddr() << ", "
       << ntohs(rewritten_flow.sport()) << ")";
    unparse_ports(sa, direction, now);
}

// ICMPPingRewriter

ICMPPingRewriter::ICMPPingRewriter()
{
#if HAVE_USER_MULTITHREAD
    _maps_no = ( click_max_cpu_ids() == 0 )? 1 : click_max_cpu_ids();
    _allocator = new SizedHashAllocator<sizeof(ICMPPingFlow)>[_maps_no];
    //click_chatter("[%s]: Allocated %d ICMP flow maps", class_name(), _maps_no);
#endif
}

ICMPPingRewriter::~ICMPPingRewriter()
{
#if HAVE_USER_MULTITHREAD
    if ( _allocator )
        delete [] _allocator;
#endif
}

void *
ICMPPingRewriter::cast(const char *n)
{
    if (strcmp(n, "IPRewriterBase") == 0)
	return static_cast<IPRewriterBase *>(this);
    else if (strcmp(n, "ICMPPingRewriter") == 0)
	return static_cast<ICMPPingRewriter *>(this);
    else
	return 0;
}

int
ICMPPingRewriter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    // numbers in seconds
    for (unsigned i=0; i<_mem_units_no; i++) {
        _timeouts[i][0] = 5 * 60;	// best effort: 5 minutes
    }

    bool dst_anno = true, has_reply_anno = false;
    int reply_anno;

    if (Args(this, errh).bind(conf)
	.read("DST_ANNO", dst_anno)
	.read("REPLY_ANNO", AnnoArg(1), reply_anno).read_status(has_reply_anno)
	.consume() < 0)
	return -1;

    _annos = (dst_anno ? 1 : 0) + (has_reply_anno ? 2 + (reply_anno << 2) : 0);
    return IPRewriterBase::configure(conf, errh);
}

IPRewriterEntry *
ICMPPingRewriter::get_entry(int ip_p, const IPFlowID &xflowid, int input)
{
    if (ip_p != IP_PROTO_ICMP)
	return 0;
    bool echo = (input != get_entry_reply);
    IPFlowID flowid(xflowid.saddr(), xflowid.sport() + !echo,
		    xflowid.daddr(), xflowid.sport() + echo);
    IPRewriterEntry *m = _state->map.get(flowid);
    if (!m && (unsigned) input < (unsigned) _input_specs.size()) {
	IPRewriterInput &is = _input_specs[input];
	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
	if (is.rewrite_flowid(flowid, rewritten_flowid, 0) == rw_addmap) {
	    rewritten_flowid.set_dport(rewritten_flowid.sport() + 1);
	    m = ICMPPingRewriter::add_flow(IP_PROTO_ICMP, flowid, rewritten_flowid, input);
	}
    }
    return m;
}

IPRewriterEntry *
ICMPPingRewriter::add_flow(int, const IPFlowID &flowid,
			   const IPFlowID &rewritten_flowid, int input)
{
    void *data;
    if ((uint16_t) (flowid.sport() + 1) != flowid.dport()
	|| (uint16_t) (rewritten_flowid.sport() + 1) != rewritten_flowid.dport()
	|| !(data = _allocator[click_current_cpu_id()].allocate()))
	return 0;

    ICMPPingFlow *flow = new(data) ICMPPingFlow
	(&_input_specs[input], flowid, rewritten_flowid,
	 !!_timeouts[click_current_cpu_id()][1], click_jiffies() +
         relevant_timeout(_timeouts[click_current_cpu_id()]), input);

    return store_flow(flow, input, _state->map);
}

int
ICMPPingRewriter::process(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    click_ip *iph = p->ip_header();
    click_icmp_echo *icmph = reinterpret_cast<click_icmp_echo *>(p->icmp_header());

    // handle non-first fragments
    if (iph->ip_p != IP_PROTO_ICMP
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < 6
	|| (icmph->icmp_type != ICMP_ECHO && icmph->icmp_type != ICMP_ECHOREPLY)) {
    mapping_fail:
        const IPRewriterInput &is = _input_specs[port];
        if (is.kind == IPRewriterInput::i_nochange) {
            return is.foutput;
        } else {
            return -1;
        }
    }

    bool echo = icmph->icmp_type == ICMP_ECHO;
    IPFlowID flowid(iph->ip_src, icmph->icmp_identifier + !echo,
		    iph->ip_dst, icmph->icmp_identifier + echo);

    IPRewriterEntry *m = _state->map.get(flowid);

    if (!m && !echo)
	goto mapping_fail;
    else if (!m) {		// create new mapping
	IPRewriterInput &is = _input_specs.unchecked_at(port);
	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();
	int result = is.rewrite_flowid(flowid, rewritten_flowid, p);
	if (result == rw_addmap) {
	    rewritten_flowid.set_dport(rewritten_flowid.sport() + 1);
	    m = ICMPPingRewriter::add_flow(IP_PROTO_ICMP, flowid, rewritten_flowid, port);
	}
	if (!m) {
	    return result;
	} else if (_annos & 2)
	    m->flow()->set_reply_anno(p->anno_u8(_annos >> 2));
    }

    ICMPPingFlow *mf = static_cast<ICMPPingFlow *>(m->flow());
    mf->apply(p, m->direction(), _annos);
    mf->change_expiry_by_timeout(
        _heap[click_current_cpu_id()],
        click_jiffies(),
        _timeouts[click_current_cpu_id()]
    );
    return m->output();
}

void
ICMPPingRewriter::push(int port, Packet *p_in) {
    checked_output_push(process(port, p_in),p_in);
}

#if HAVE_BATCH
void
ICMPPingRewriter::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this,port](Packet*p){return process(port,p);};
    CLASSIFY_EACH_PACKET(noutputs() + 1,fnt,batch,checked_output_push_batch);
}
#endif


String
ICMPPingRewriter::dump_mappings_handler(Element *e, void *)
{
    ICMPPingRewriter *rw = (ICMPPingRewriter *)e;
    StringAccum sa;
    click_jiffies_t now = click_jiffies();
    for (Map::iterator iter = rw->_state->map.begin(); iter.live(); ++iter) {
	ICMPPingFlow *f = static_cast<ICMPPingFlow *>(iter->flow());
	f->unparse(sa, iter->direction(), now);
	sa << '\n';
    }
    return sa.take_string();
}

void
ICMPPingRewriter::add_handlers()
{
    add_read_handler("table", dump_mappings_handler);
    add_read_handler("mappings", dump_mappings_handler, 0, Handler::h_deprecated);
    add_rewriter_handlers(true);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBase)
EXPORT_ELEMENT(ICMPPingRewriter)
