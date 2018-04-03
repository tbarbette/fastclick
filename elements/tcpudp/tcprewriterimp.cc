// -*- c-basic-offset: 4 -*-
/*
 * tcprewriter.{cc,hh} -- rewrites packet source and destination
 * Eddie Kohler
 *
 * Per-core, thread safe data structures and batching by Georgios Katsikas
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 * Copyright (c) 2008-2010 Meraki, Inc.
 * Copyright (c) 2016 KTH Royal Institute of Technology
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
#include "tcprewriterimp.hh"
#include <clicknet/ip.h>
#include <clicknet/tcp.h>
#include <click/args.hh>
#include <click/straccum.hh>
#include <click/error.hh>
CLICK_DECLS

// TCPRewriterIMP

TCPRewriterIMP::TCPRewriterIMP() : _allocator()
{
}

TCPRewriterIMP::~TCPRewriterIMP()
{
}

void *
TCPRewriterIMP::cast(const char *n)
{
    if (strcmp(n, "IPRewriterBase") == 0)
	return (IPRewriterBaseIMP *)this;
    else if (strcmp(n, "TCPRewriterIMP") == 0)
	return (TCPRewriterIMP *)this;
    else
	return 0;
}

int
TCPRewriterIMP::configure(Vector<String> &conf, ErrorHandler *errh)
{
    uint32_t timeouts[2];

    // numbers in seconds
    timeouts[0] = 300;		// nodata: 5 minutes (should be > TCP_DONE)
    timeouts[1] = default_guarantee;

    _tcp_data_timeout = 86400;	// 24 hours
    _tcp_done_timeout = 240;	// 4 minutes
    bool dst_anno = true, has_reply_anno = false;
    int reply_anno;

    if (Args(this, errh).bind(conf)
	.read("TCP_NODATA_TIMEOUT", SecondsArg(), timeouts[0])
	.read("TCP_GUARANTEE", SecondsArg(), timeouts[1])
	.read("TIMEOUT", SecondsArg(), _tcp_data_timeout)
	.read("TCP_TIMEOUT", SecondsArg(), _tcp_data_timeout)
	.read("TCP_DONE_TIMEOUT", SecondsArg(), _tcp_done_timeout)
	.read("DST_ANNO", dst_anno)
	.read("REPLY_ANNO", AnnoArg(1), reply_anno).read_status(has_reply_anno)
	.consume() < 0)
	return -1;

    initialize_timeout(0, timeouts[0]);
    initialize_timeout(1, timeouts[1]);

    _annos = (dst_anno ? 1 : 0) + (has_reply_anno ? 2 + (reply_anno << 2) : 0);
    _tcp_data_timeout *= CLICK_HZ; // IPRewriterBase handles the others
    _tcp_done_timeout *= CLICK_HZ;

    return IPRewriterBaseIMP::configure(conf, errh);
}

IPRewriterEntry *
TCPRewriterIMP::add_flow(int /*ip_p*/, const IPFlowID &flowid,
		      const IPFlowID &rewritten_flowid, int input)
{
    void *data;
    if (!(data = _allocator->allocate()))
	return 0;

    TCPFlow *flow = new(data) TCPFlow
	((IPRewriterInput*)&input_specs(input), flowid, rewritten_flowid,
	 !!timeouts()[1], click_jiffies() +
         relevant_timeout(timeouts()));

    return store_flow(flow, input, map());
}

int
TCPRewriterIMP::process(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    if (!p) {
        return -1;
    }

    click_ip *iph = p->ip_header();

    // handle non-first fragments
    if (iph->ip_p != IP_PROTO_TCP
	|| !IP_FIRSTFRAG(iph)
	|| p->transport_length() < 8) {
	const IPRewriterInputIMP &is = input_specs(port);
	    if (is.kind == IPRewriterInputAncestor::i_nochange)
            return is.foutput;
        else
            return -1;
    }

    IPFlowID flowid(p);
    IPRewriterEntry *m = map().get(flowid);

    if (!m) {			// create new mapping
    	IPRewriterInputIMP &is = input_specs_unchecked(port);
    	IPFlowID rewritten_flowid = IPFlowID::uninitialized_t();

		int result = is.rewrite_flowid(flowid, rewritten_flowid, p);
		if (result == rw_addmap) {
			m = TCPRewriterIMP::add_flow(IP_PROTO_TCP, flowid, rewritten_flowid, port);
        }

		if (!m) {
			return result;
		} else if (_annos & 2) {
			m->flowimp()->set_reply_anno(p->anno_u8(_annos >> 2));
        }
    }

    TCPFlow *mf = static_cast<TCPFlow *>(m->flowimp());
    mf->apply(p, m->direction(), _annos);

    click_jiffies_t now_j = click_jiffies();
    if (timeouts()[1])
    	mf->change_expiry(heap(), true, now_j + timeouts()[1]);
    else
    	mf->change_expiry(heap(), false, now_j + tcp_flow_timeout(mf));

    return m->output();
}

void
TCPRewriterIMP::push(int port, Packet *p)
{
    int output_port = process(port, p);
    if ( output_port < 0 ) {
        p->kill();
        return;
    }

    output(output_port).push(p);
}

#if HAVE_BATCH
void
TCPRewriterIMP::push_batch(int port, PacketBatch *batch)
{
    auto fnt = [this,port](Packet*p){return process(port,p);};
    CLASSIFY_EACH_PACKET(noutputs() + 1,fnt,batch,checked_output_push_batch);
}
#endif

String
TCPRewriterIMP::tcp_mappings_handler(Element *e, void *)
{
    TCPRewriterIMP *rw = (TCPRewriterIMP *)e;
    click_jiffies_t now = click_jiffies();
    StringAccum sa;
    for (Map::iterator iter = rw->map().begin(); iter.live(); ++iter) {
	TCPFlow *f = static_cast<TCPFlow *>(iter->flowimp());
	f->unparse(sa, iter->direction(), now);
	sa << '\n';
    }
    return sa.take_string();
}

int
TCPRewriterIMP::tcp_lookup_handler(int, String &str, Element *e, const Handler *, ErrorHandler *errh)
{
    TCPRewriterIMP *rw = (TCPRewriterIMP *)e;
    IPAddress saddr, daddr;
    unsigned short sport, dport;

    if (Args(rw, errh).push_back_words(str)
	.read_mp("SADDR", saddr)
	.read_mp("SPORT", IPPortArg(IP_PROTO_TCP), sport)
	.read_mp("DADDR", daddr)
	.read_mp("DPORT", IPPortArg(IP_PROTO_TCP), dport)
	.complete() < 0)
	return -1;

    HashContainer<IPRewriterEntry> *map = rw->get_map(IPRewriterInput::mapid_default);
    if (!map)
	return errh->error("no map!");

    StringAccum sa;
    IPFlowID flow(saddr, htons(sport), daddr, htons(dport));
    if (Map::iterator iter = map->find(flow)) {
	TCPFlow *f = static_cast<TCPFlow *>(iter->flowimp());
	const IPFlowID &flowid = f->entry(iter->direction()).rewritten_flowid();

	sa << flowid.saddr() << " " << ntohs(flowid.sport()) << " "
	   << flowid.daddr() << " " << ntohs(flowid.dport());
    }

    str = sa.take_string();
    return 0;
}

void
TCPRewriterIMP::add_handlers()
{
    add_read_handler("table", tcp_mappings_handler, 0);
    add_read_handler("mappings", tcp_mappings_handler, 0, Handler::h_deprecated);
    set_handler("lookup", Handler::OP_READ | Handler::READ_PARAM, tcp_lookup_handler, 0);
    add_rewriter_handlers(true);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(IPRewriterBaseIMP)
EXPORT_ELEMENT(TCPRewriterIMP)
